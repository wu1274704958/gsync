#pragma once
#include <type_traits>
#include <cstring>
#include "def.h"
#include "../common.h"
#include "connection_events.hpp"
#include "engine_stages.hpp"
#include "core/server.h"
#include "mqas/core/engine.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/engine_settings.h"
#include "mqas/io/ip.h"
#include "mqas/comm/locator.h"

namespace gsync::stages {
struct ServerPrepare {
    std::shared_ptr<ServerEntry> server;
    GSY_EngineId engine_id;
    const toml::value& config;
    sockaddr& address;
    std::shared_ptr<mqas::io::UdpSocket>& socket;
    std::shared_ptr<const mqas::core::TlsConfiguration>& tls;
    bool& address_ready;
    bool& allow_shared_endpoint;
};
struct ServerReuse {
    std::shared_ptr<ServerEntry> server;
    std::shared_ptr<ServerEngineEntry> owner;
    mqas::core::EngineSettingsView& running;
};
struct ServerEngineReady {
    std::shared_ptr<ServerEntry> server;
    std::shared_ptr<ServerEngineEntry> owner;
    const std::shared_ptr<mqas::io::UdpSocket>& socket;
    const toml::value& config;
};
struct ServerIncoming {
    std::shared_ptr<ServerEntry> server;
    std::shared_ptr<mqas::core::IConnect> connection;
    GSY_ConnectionHwnd handle = InvalidConnection;
    bool notify_connected = false;
};
struct ServerCleanup { std::shared_ptr<ServerEntry> server; };

}

extern bool server_shutting_down;

template<GSY_EngineId Id, typename Config, typename C>
void setup_server_connect_signals(mqas::core::engine<C>* engine, const std::shared_ptr<ServerEngineEntry>& owner)
{
    std::weak_ptr<ServerEngineEntry> weak_owner = owner;
    engine->on_connect_closed_signal.connect([weak_owner](std::shared_ptr<C>) {
        if (auto entry = weak_owner.lock()) push_task([entry] { cleanup_server_engine(entry); }, true);
    });
    engine->on_new_connect_signal.connect([weak_owner](std::shared_ptr<C> conn) {
        auto entry = weak_owner.lock();
        if (!entry) { conn->close(); return; }
        for (const auto& server : entry->servers) {
            if (server->stopping || !server->ctx || !server->ctx->on_client_connect) continue;
            gsync::stages::ServerIncoming incoming{server, conn};
            if (gsync::stages::run<gsync::stages::Server::Authenticate, Id, Config>(incoming) != EC_Ok) continue;
            const auto conn_hwnd = get_new_connection_hwnd(server->hwnd + SERVER_ENGINE_ID_OFFSET);
            if (conn_hwnd == InvalidConnection) {
                if (server->ctx->on_error) server->ctx->on_error(EC_ConnectOverLimit, server->hwnd);
                conn->close();
                return;
            }
            auto* conn_ctx = server->ctx->on_client_connect(server->hwnd, conn_hwnd);
            if (!conn_ctx) {
                recycle_connection_hwnd(conn_hwnd);
                continue;
            }
            incoming.handle = conn_hwnd;
            const auto accepted = gsync::stages::run<gsync::stages::Server::Accepted, Id, Config>(incoming);
            if (accepted != EC_Ok) { recycle_connection_hwnd(conn_hwnd); conn->close(); return; }
            {
                std::lock_guard lock(connect_mutex);
                connect_map.insert({conn_hwnd, conn});
            }
            server->connections.emplace(conn_hwnd, conn);
            conn->set_connection_config(server->config);
            setup_base_stream_events(*conn, conn_hwnd, conn_ctx);
            conn->on_close_signal.connect([server, conn_hwnd, conn_ctx](mqas::core::IConnect&) {
                destroy_connect(conn_hwnd);
                server->connections.erase(conn_hwnd);
                if (conn_ctx->on_disconnect) conn_ctx->on_disconnect(EC_Disconnected, conn_hwnd);
                if (server->ctx && server->ctx->on_client_disconnect)
                    server->ctx->on_client_disconnect(server->hwnd, conn_hwnd);
                finish_server_stop(server);
            });
            conn->on_hsk_done_signal.connect([conn_hwnd, conn_ctx](mqas::core::IConnect&, ::lsquic_hsk_status status) {
                if (status == LSQ_HSK_OK && conn_ctx->on_connect) conn_ctx->on_connect(EC_Ok, conn_hwnd);
            });
            if (incoming.notify_connected && conn_ctx->on_connect) conn_ctx->on_connect(EC_Ok, conn_hwnd);
            return;
        }
        conn->close();
    });
}

template<size_t I, typename TU, typename Config>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ServerHwnd launch_server_internal(GSY_EngineId engine_id, const char* config_file, GSY_BaseServerContext* ctx)
{
    if (engine_id - 1 != I) {
        if constexpr (I + 1 < std::tuple_size_v<TU>) return launch_server_internal<I + 1, TU, Config>(engine_id, config_file, ctx);
        if (ctx && ctx->on_error) ctx->on_error(EC_EngineNotMatch, InvalidServer);
        return InvalidServer;
    }
    if (!is_running || !config_file || !ctx) return InvalidServer;
    using EngineType = std::tuple_element_t<I, TU>;
    auto server = std::make_shared<ServerEntry>();
    server->ctx = ctx;
    server->cleanup = [](const auto& entry) {
        gsync::stages::ServerCleanup frame{entry};
        gsync::stages::run<gsync::stages::Server::Cleanup, I + 1, Config>(frame);
    };
    using Descriptor = typename gsync::stages::Lookup<I + 1, Config>::type;
    if (ctx->options && std::is_void_v<typename Descriptor::option_type>) return InvalidServer;
    gsync::stages::ServerStart frame{ctx, server};
    if (gsync::stages::run<gsync::stages::Server::Validate, I + 1, Config>(frame) != EC_Ok)
        return InvalidServer;
    {
        std::lock_guard lock(server_mutex);
        if (server_shutting_down || !is_running) return InvalidServer;
        server->hwnd = get_new_server_hwnd(engine_id);
        if (server->hwnd != InvalidServer) server_map.emplace(server->hwnd, server);
    }
    if (server->hwnd == InvalidServer) {
        if (ctx->on_error) ctx->on_error(EC_ConnectOverLimit, InvalidServer);
        return InvalidServer;
    }
    push_task([server, engine_id, path = std::string(config_file)] {
        if (server->stopping) { finish_server_stop(server); return; }
        std::shared_ptr<ServerEngineEntry> created;
        try {
            auto parsed = std::make_shared<toml::value>(toml::parse(path));
            mqas::core::engine_driver::normalize_engine_config(parsed, path.c_str());
            sockaddr bind_addr{};
            std::shared_ptr<mqas::io::UdpSocket> socket;
            std::shared_ptr<const mqas::core::TlsConfiguration> tls;
            bool address_ready = false;
            bool allow_shared_endpoint = false;
            gsync::stages::ServerPrepare prepare{server, engine_id, *parsed, bind_addr, socket, tls,
                address_ready, allow_shared_endpoint};
            const auto prepare_error = gsync::stages::run<gsync::stages::Server::BeforeEngine, I + 1, Config>(prepare);
            if (prepare_error != EC_Ok) { fail_server_start(server, prepare_error); return; }
            if (!address_ready) {
                const auto config = toml::find<mqas::core::engine_config>(*parsed, "engine_config");
                if (!mqas::io::Ip::str2addr_ipv4(config.bind_ip.c_str(), config.port, bind_addr)) {
                    fail_server_start(server, EC_InvalidAddress); return;
                }
            }
            const auto port = mqas::io::Ip::addr_get_port(bind_addr);
            EngineKey key{engine_id, mqas::io::Ip::addr2str_ipv4(bind_addr), port};
            std::shared_ptr<ServerEngineEntry> owner;
            if (port != 0) {
                if (auto it = server_engines.find(key); it != server_engines.end()) {
                    owner = it->second;
                    auto* engine = dynamic_cast<EngineType*>(owner->engine);
                    if (!engine) { fail_server_start(server, EC_EngineNotMatch); return; }
                    ::lsquic_engine_settings storage{};
                    const auto candidate = mqas::core::prepare_engine_settings(*parsed, mqas::core::EngineFlags::Server, bind_addr, storage);
                    auto running = engine->get_settings_view();
                    gsync::stages::ServerReuse reuse{server, owner, running};
                    const auto reuse_error = gsync::stages::run<gsync::stages::Server::Reuse, I + 1, Config>(reuse);
                    if (reuse_error != EC_Ok) { fail_server_start(server, reuse_error); return; }
                    const auto running_hash = mqas::core::EngineSettingsHasher{}(running);
                    if (running_hash != mqas::core::EngineSettingsHasher{}(candidate) || running != candidate) {
                        fail_server_start(server, EC_EngineConfigMismatch); return;
                    }
                } else if (server_endpoint_overlaps(bind_addr)) {
                    fail_server_start(server, EC_AlreadyExists); return;
                }
                if (!allow_shared_endpoint) for (const auto& [existing, entry] : engine_map)
                    if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), bind_addr)) {
                        fail_server_start(server, EC_AlreadyExists); return;
                    }
            }
            if (!owner) {
                auto engine = std::make_unique<EngineType>(*io_cxt);
                engine->init(*parsed, path.c_str(), mqas::core::EngineFlags::Server, socket, std::move(tls));
                key.port = mqas::io::Ip::addr_get_port(engine->get_local_addr());
                if (server_endpoint_overlaps(engine->get_local_addr())) { fail_server_start(server, EC_AlreadyExists); return; }
                if (!allow_shared_endpoint) for (const auto& [existing, entry] : engine_map)
                    if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), engine->get_local_addr())) {
                        fail_server_start(server, EC_AlreadyExists); return;
                    }
                owner = std::make_shared<ServerEngineEntry>();
                owner->key = key;
                std::memcpy(&owner->bind_addr, &engine->get_local_addr(), sizeof(sockaddr_in));
                owner->engine = engine.get();
                gsync::stages::ServerEngineReady initialized{server, owner, socket, *parsed};
                const auto init_error = gsync::stages::run<gsync::stages::Server::InitEngine, I + 1, Config>(initialized);
                if (init_error != EC_Ok) { fail_server_start(server, init_error); return; }
                setup_server_connect_signals<I + 1, Config>(engine->get_engine().get(), owner);
                server_engines.emplace(key, owner);
                created = owner;
                auto* running_engine = engine.release();
                running_engine->start_recv();
                running_engine->process_conns();
            } else {
                gsync::stages::ServerEngineReady reused{server, owner, socket, *parsed};
                const auto error = gsync::stages::run<gsync::stages::Server::InitEngine, I + 1, Config>(reused);
                if (error != EC_Ok) { fail_server_start(server, error); return; }
            }
            server->config = std::move(parsed);
            server->owner = owner;
            owner->servers.push_back(server);
            server->started = true;
            if (server->stopping) { finish_server_stop(server); return; }
            if (server->ctx->on_started) server->ctx->on_started(EC_Ok, server->hwnd);
        } catch (...) {
            if (created) {
                auto it = server_engines.find(created->key);
                if (it != server_engines.end() && it->second == created) server_engines.erase(it);
                if (created->engine) { auto* engine = created->engine; created->engine = nullptr; destroy_engine(engine); }
            }
            fail_server_start(server, EC_ConnectFailed);
        }
    }, true);
    return server->hwnd;
}
#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_SERVER(ET, CT) \
GSY_ServerHwnd GSYNC_EXTERN GSY_launch_server(GSY_EngineId engine_id, const char* config_file, GSY_BaseServerContext* cxt) { \
    return launch_server_internal<0, ET, CT>(engine_id, config_file, cxt); \
}
