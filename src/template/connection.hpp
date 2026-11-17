#pragma once
#include <type_traits>
#include <cstring>
#include "def.h"
#include "../common.h"
#include "core/connection.h"
#include "mqas/core/engine_driver.h"
#include "mqas/io/ip.h"
#include "mqas/core/connect.h"



// All registry operations below run on the SDK I/O thread.
template<typename ET>
requires std::is_base_of_v<mqas::core::engine_base_interface, ET>
bool handle_new_connection_internal(GSY_ConnectionHwnd hwnd, ET* engine, const std::string& ip,
    short port, GSY_BaseConnectionContext* context, std::weak_ptr<EngineEntry> entry,
    std::shared_ptr<const toml::value> connection_config);

template<size_t I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ConnectionHwnd connect_internal(GSY_EngineId engine_id, const char* config_file,
    const char* ip, short port, GSY_BaseConnectionContext* context) {
    if (engine_id - 1 != I) {
        if constexpr (I + 1 < std::tuple_size_v<TU>)
            return connect_internal<I + 1, TU>(engine_id, config_file, ip, port, context);
        return InvalidConnection;
    }
    if (!is_running || !config_file || !ip || !context) return InvalidConnection;
    const auto hwnd = get_new_connection_hwnd(engine_id);
    if (hwnd == InvalidConnection) return hwnd;
    using EngineType = std::tuple_element_t<I, TU>;
    push_task([hwnd, engine_id, path = std::string(config_file), remote = std::string(ip), port, context] {
        std::shared_ptr<EngineEntry> created;
        try {
            auto parsed = std::make_shared<toml::value>(toml::parse(path));
            mqas::core::engine_driver::normalize_engine_config(parsed, path.c_str());
            const auto config = toml::find<mqas::core::engine_config>(*parsed, "engine_config");
            sockaddr bind_addr{};
            if (!mqas::io::Ip::str2addr_ipv4(config.bind_ip.c_str(), config.port, bind_addr)) {
                if (context->on_error) context->on_error(EC_InvalidAddress, hwnd);
                recycle_connection_hwnd(hwnd);
                return;
            }
            const auto bind_ip = mqas::io::Ip::addr2str_ipv4(bind_addr);
            EngineKey key{engine_id, bind_ip, config.port};
            if (config.port != 0) {
                if (server_endpoint_overlaps(bind_addr)) {
                    if (context->on_error) context->on_error(EC_AlreadyExists, hwnd);
                    recycle_connection_hwnd(hwnd);
                    return;
                }
                if (auto it = engine_map.find(key); it != engine_map.end()) {
                    ::lsquic_engine_settings settings_storage{};
                    const auto candidate = mqas::core::prepare_engine_settings(*parsed, mqas::core::EngineFlags::None, bind_addr, settings_storage);
                    auto* engine = dynamic_cast<EngineType*>(it->second->engine);
                    if (!engine) {
                        if (context->on_error) context->on_error(EC_EngineNotMatch, hwnd);
                        recycle_connection_hwnd(hwnd);
                        return;
                    }
                    if (engine->get_settings_hash() != mqas::core::EngineSettingsHasher{}(candidate) || engine->get_settings_view() != candidate) {
                        if (context->on_error) context->on_error(EC_EngineConfigMismatch, hwnd);
                        recycle_connection_hwnd(hwnd);
                        return;
                    }
                    if (!handle_new_connection_internal(hwnd, engine, remote, port, context, it->second, parsed))
                        recycle_connection_hwnd(hwnd);
                    return;
                }
                for (const auto& [existing, entry] : engine_map) {
                    if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), bind_addr)) {
                        if (context->on_error) context->on_error(EC_AlreadyExists, hwnd);
                        recycle_connection_hwnd(hwnd);
                        return;
                    }
                }
            }
            auto engine = std::make_unique<EngineType>(*io_cxt);
            engine->init(*parsed, path.c_str(), mqas::core::EngineFlags::None);
            key.port = mqas::io::Ip::addr_get_port(engine->get_local_addr());
            if (server_endpoint_overlaps(engine->get_local_addr())) {
                if (context->on_error) context->on_error(EC_AlreadyExists, hwnd);
                recycle_connection_hwnd(hwnd);
                return;
            }
            for (const auto& [existing, entry] : engine_map) {
                if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), engine->get_local_addr())) {
                    if (context->on_error) context->on_error(EC_AlreadyExists, hwnd);
                    recycle_connection_hwnd(hwnd);
                    return;
                }
            }
            auto entry = std::make_shared<EngineEntry>();
            entry->key = key;
            std::memcpy(&entry->bind_addr, &engine->get_local_addr(), sizeof(sockaddr_in));
            entry->engine = engine.get();
            engine->start_recv();
            engine->process_conns();
            engine_map.emplace(key, entry);
            created = entry;
            if (!handle_new_connection_internal(hwnd, engine.get(), remote, port, context, entry, parsed)) {
                engine_map.erase(key);
                recycle_connection_hwnd(hwnd);
                return;
            }
            engine.release();
            created.reset();
        } catch (...) {
            if (created) {
                auto it = engine_map.find(created->key);
                if (it != engine_map.end() && it->second == created) engine_map.erase(it);
            }
            connection_engines.erase(hwnd);
            if (context->on_error) context->on_error(EC_ConnectFailed, hwnd);
            destroy_connect(hwnd);
            recycle_connection_hwnd(hwnd);
        }
    }, true);
    return hwnd;
}

template<typename ET>
requires std::is_base_of_v<mqas::core::engine_base_interface, ET>
bool handle_new_connection_internal(const GSY_ConnectionHwnd hwnd,ET* engine,const std::string& ip,
    short port,GSY_BaseConnectionContext* context, std::weak_ptr<EngineEntry> entry,
    std::shared_ptr<const toml::value> connection_config)
{
    sockaddr addr{};
    if(!mqas::io::Ip::str2addr_ipv4(ip.c_str(), port, addr))
    {
        if (context->on_error)
            context->on_error(EC_InvalidAddress,hwnd);
        return false;
    }

    auto connect = engine->get_engine()->connect(addr, N_LSQVER);
    engine->get_engine()->whitelist_addr.push_back(addr);
    engine->get_engine()->whitelist_port.push_back(mqas::io::Ip::addr_get_port(addr));
    auto conn = connect.lock();
    if (!conn)
    {
        if (context->on_error)
            context->on_error(EC_ConnectFailed,hwnd);
        return false;
    }


    {
        std::lock_guard<std::mutex> _lock(connect_mutex);
        connect_map.insert({hwnd, connect});
    }

    connection_engines[hwnd] = entry;
    conn->set_connection_config(std::move(connection_config));
    conn->set_cxt(context);

    conn->on_close_signal.connect([hwnd,context,entry](mqas::core::IConnect& c)
    {
        p2p_session_remove_connection(hwnd);
        auto owner = connection_engines.find(hwnd);
        if (owner != connection_engines.end() && owner->second.lock() == entry.lock())
            connection_engines.erase(owner);
        push_task([entry] { cleanup_connection_engine(entry); }, true);
        destroy_connect(hwnd);
        if (context->on_disconnect)
        {
            context->on_disconnect(EC_Disconnected,hwnd);
        }
    });
    conn->on_hsk_done_signal.connect([hwnd,context](mqas::core::IConnect& c,::lsquic_hsk_status status)
    {
        if (status == ::lsquic_hsk_status::LSQ_HSK_OK)
        {
            if (context->on_connect)
            {
                context->on_connect(EC_Ok,hwnd);
            }
        }
    });
    if (context->on_stream_open)
    {
        conn->on_new_stream_signal.connect([hwnd,context](std::shared_ptr<mqas::core::IStream> stream)
        {
            context->on_stream_open(hwnd,reinterpret_cast<GSY_StreamId>(stream->get_origin()),EC_Ok);
        });
    }
    conn->on_stream_close_signal.connect([hwnd,context](std::shared_ptr<mqas::core::IStream> stream)
    {
        const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
        p2p_session_remove_stream(hwnd, sid);
        if (context->on_stream_close) context->on_stream_close(hwnd, sid, EC_Ok);
    });
    return true;
}


#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_CONNECTION(ET)                                              \
GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,     \
        const char* ip,short port,GSY_BaseConnectionContext* cxt) {                             \
    return connect_internal<0,ET>(engine_id,config_file,ip,port,cxt);                           \
}
