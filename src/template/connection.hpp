#pragma once
#include <type_traits>
#include "def.h"
#include "../common.h"
#include "core/connection.h"
#include "mqas/core/engine_driver.h"
#include "mqas/io/ip.h"



template<typename ET>
requires std::is_base_of_v<mqas::core::engine_base_interface, ET>
bool handle_new_connection_internal(const GSY_ConnectionHwnd hwnd,ET* engine,const std::string& ip,
    short port,GSY_BaseConnectionContext* context);

template<size_t I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ConnectionHwnd connect_internal(GSY_EngineId engine_id,const char* config_file,const char* ip,short port,GSY_BaseConnectionContext* context) {
    if (engine_id - 1 == I) {
        using EngineType = std::tuple_element_t<I, TU>;
        const GSY_ConnectionHwnd hwnd = get_new_connection_hwnd(engine_id);
        if (hwnd == InvalidConnection) {
            if (global_context->on_error)
                global_context->on_error("Connection create handle failed",EC_ConnectOverLimit);
            return InvalidConnection;
        }
        std::lock_guard<std::mutex> _lock(engines_mutex);
        if (!engine_map.contains(engine_id)) {
            auto task = [
                config_file_path = std::string(config_file),
                ip_str = std::string(ip),
                port,
                context,hwnd]() {
                auto engine = new EngineType(*io_cxt);
                engine->init(config_file_path.c_str(),mqas::core::EngineFlags::None);
                engine->start_recv();
                engine->process_conns();

                if (!handle_new_connection_internal(hwnd,engine,ip_str,port,context))
                {
                    delete engine;
                    return;
                }
                engine_map.insert({hwnd / MAX_CONNECTION_HWND, engine});
            };
            push_task(task);
        }else {
            auto engine = dynamic_cast<EngineType*>(engine_map[engine_id]);
            if (engine == nullptr) {
                recycle_connection_hwnd(hwnd);
                if (context->on_error)
                    context->on_error(EC_EngineNotMatch,hwnd);
                return InvalidConnection;
            }
            auto task = [engine,hwnd,context,ip_str = std::string(ip),port]() {
                handle_new_connection_internal(hwnd,engine,ip_str,port,context);
            };
            push_task(task);
        }
        return hwnd;
    }else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            return InvalidConnection;
        }else {
            return connect_internal<I + 1,TU>(engine_id,config_file,ip,port,context);
        }
    }
}

template<typename ET>
requires std::is_base_of_v<mqas::core::engine_base_interface, ET>
bool handle_new_connection_internal(const GSY_ConnectionHwnd hwnd,ET* engine,const std::string& ip,
    short port,GSY_BaseConnectionContext* context)
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

    engine->get_engine()->on_connect_closed_signal.connect([engine,hwnd](std::shared_ptr<mqas::core::IConnect>)
    {
        if (engine->get_engine()->connect_count() - 1 <= 0)
        {
            push_task([hwnd]() { destroy_engine_by_conn_hwnd(hwnd); });
        }
    });

    {
        std::lock_guard<std::mutex> _lock(connect_mutex);
        connect_map.insert({hwnd, connect});
    }

    conn->set_cxt(context);

    conn->on_close_signal.connect([hwnd,context](mqas::core::IConnect& c)
    {
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
    if (context->on_stream_close)
    {
        conn->on_stream_close_signal.connect([hwnd,context](std::shared_ptr<mqas::core::IStream> stream)
        {
            context->on_stream_close(hwnd,reinterpret_cast<GSY_StreamId>(stream->get_origin()),EC_Ok);
        });
    }
    return true;
}


#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_CONNECTION(ET)                                              \
GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,     \
        const char* ip,short port,GSY_BaseConnectionContext* cxt) {                             \
    return connect_internal<0,ET>(engine_id,config_file,ip,port,cxt);                           \
}                                                                                               \
