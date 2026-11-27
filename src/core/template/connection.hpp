#pragma once

#include "../common.h"
#include "connection_events.hpp"
#include "engine_stages.hpp"
#include "mqas/core/engine_driver.h"
#include "mqas/core/connect.h"
#include "mqas/io/ip.h"
#include "mqas/io/timer.h"

namespace gsync::stages {

// Validate runs on the caller's thread. All subsequent stages run on the I/O thread.
struct ConnectAttempt {
    const GSY_ContextExtension* options = nullptr; // Only valid during Validate.
    GSY_BaseConnectionContext* context = nullptr;
    GSY_ConnectionHwnd handle = InvalidConnection;
    std::string path;
    std::shared_ptr<toml::value> config;
    mqas::io::SocketAddress remote{};
    std::shared_ptr<void> extension;
    std::shared_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<const mqas::core::TlsConfiguration> tls;
    std::string server_name;
    uint64_t handshake_timeout_ms = 0;

    mqas::core::engine_base_interface* engine = nullptr;
    std::shared_ptr<mqas::core::engine_base_interface> private_engine;
    std::weak_ptr<EngineEntry> shared_engine;
    std::weak_ptr<mqas::core::IConnect> transport;
    std::shared_ptr<mqas::io::Timer> timer;
    bool connected = false;
    bool closing = false;
    bool terminal_scheduled = false;
    ErrorCode close_reason = EC_Disconnected;
};

template<GSY_EngineId Id, class Config>
void finish_connection(const std::shared_ptr<ConnectAttempt>& attempt) {
    if (std::exchange(attempt->terminal_scheduled, true)) return;
    attempt->closing = true;
    if (attempt->timer && attempt->timer->is_start()) attempt->timer->stop();
    // Engine destruction and terminal callbacks must run after lsquic's close stack.
    push_task([attempt, transport = attempt->transport.lock()]() mutable {
        run<Connect::Cleanup, Id, Config>(*attempt);
        destroy_connect(attempt->handle, false);
        transport.reset();
        if (auto owner = attempt->shared_engine.lock()) cleanup_connection_engine(owner);
        attempt->private_engine.reset();
        attempt->timer.reset();
        attempt->transport.reset();
        attempt->tls.reset();
        attempt->socket.reset();
        attempt->extension.reset();
        const auto callback = attempt->connected ? attempt->context->on_disconnect : attempt->context->on_error;
        if (callback) callback(attempt->close_reason, attempt->handle);
        unregister_outbound_connection(attempt->handle);
    }, true);
}

template<GSY_EngineId Id, class Config>
void close_connection(const std::shared_ptr<ConnectAttempt>& attempt, ErrorCode code) {
    if (std::exchange(attempt->closing, true)) return;
    attempt->close_reason = code;
    if (auto conn = attempt->transport.lock(); conn && !conn->is_closed()) {
        conn->close();
    } else {
        finish_connection<Id, Config>(attempt);
    }
}

template<GSY_EngineId Id, class Config>
void complete_handshake(const std::shared_ptr<ConnectAttempt>& attempt, mqas::core::IConnect& conn) {
    if (attempt->closing || attempt->connected) return;
    const auto security = conn.security_info();
    if (security.handshake == mqas::core::HandshakeState::Pending) return;
    auto code = security.handshake == mqas::core::HandshakeState::Succeeded
        ? run<Connect::Authenticated, Id, Config>(*attempt) : EC_ConnectFailed;
    if (code != EC_Ok) {
        close_connection<Id, Config>(attempt, code);
        return;
    }
    if (attempt->timer && attempt->timer->is_start()) attempt->timer->stop();
    attempt->connected = true;
    if (attempt->context->on_connect) attempt->context->on_connect(EC_Ok, attempt->handle);
}

template<class Engine>
ErrorCode prepare_connection_engine(ConnectAttempt& attempt, GSY_EngineId id) {
    // A supplied socket can already belong to another engine. Use a private engine
    // for this attempt, with its own TLS configuration and connection lifetime.
    if (attempt.socket || attempt.tls) {
        auto engine = std::make_shared<Engine>(*io_cxt);
        engine->init(*attempt.config, attempt.path.c_str(), mqas::core::EngineFlags::None,
            attempt.socket, attempt.tls);
        engine->start_recv();
        attempt.engine = engine.get();
        attempt.private_engine = std::move(engine);
        return EC_Ok;
    }

    const auto config = toml::find<mqas::core::engine_config>(*attempt.config, "engine_config");
    mqas::io::SocketAddress bind_addr{};
    if (!mqas::io::Ip::str2addr(config.bind_ip.c_str(), config.port, bind_addr.storage)) return EC_InvalidAddress;
    EngineKey key{id, mqas::io::Ip::addr2str(bind_addr), config.port};
    if (config.port != 0) {
        if (server_endpoint_overlaps(bind_addr)) return EC_AlreadyExists;
        if (auto it = engine_map.find(key); it != engine_map.end()) {
            auto* engine = dynamic_cast<Engine*>(it->second->engine);
            if (!engine) return EC_EngineNotMatch;
            ::lsquic_engine_settings storage{};
            const auto candidate = mqas::core::prepare_engine_settings(*attempt.config,
                mqas::core::EngineFlags::None, bind_addr, storage);
            if (engine->get_settings_hash() != mqas::core::EngineSettingsHasher{}(candidate) ||
                engine->get_settings_view() != candidate) return EC_EngineConfigMismatch;
            attempt.engine = engine;
            attempt.shared_engine = it->second;
            return EC_Ok;
        }
        for (const auto& [existing, entry] : engine_map)
            if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), bind_addr))
                return EC_AlreadyExists;
    }

    auto engine = std::make_unique<Engine>(*io_cxt);
    engine->init(*attempt.config, attempt.path.c_str(), mqas::core::EngineFlags::None);
    const auto& actual = engine->get_local_addr();
    key.port = mqas::io::Ip::addr_get_port(actual);
    if (server_endpoint_overlaps(actual)) return EC_AlreadyExists;
    for (const auto& [existing, entry] : engine_map)
        if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), actual))
            return EC_AlreadyExists;
    auto entry = std::make_shared<EngineEntry>();
    entry->key = key;
    entry->bind_addr = mqas::io::SocketAddress(actual).storage;
    entry->engine = engine.get();
    engine->start_recv();
    engine_map.emplace(key, entry);
    attempt.engine = engine.release();
    attempt.shared_engine = entry;
    return EC_Ok;
}

template<class Engine, GSY_EngineId Id, class Config>
ErrorCode start_connection(const std::shared_ptr<ConnectAttempt>& attempt) {
    attempt->config = std::make_shared<toml::value>(toml::parse(attempt->path));
    mqas::core::engine_driver::normalize_engine_config(attempt->config, attempt->path.c_str());
    auto code = run<Connect::BeforeEngine, Id, Config>(*attempt);
    if (code != EC_Ok) return code;
    code = prepare_connection_engine<Engine>(*attempt, Id);
    if (code != EC_Ok) return code;
    code = run<Connect::InitEngine, Id, Config>(*attempt);
    if (code != EC_Ok) return code;
    code = run<Connect::BeforeConnect, Id, Config>(*attempt);
    if (code != EC_Ok) return code;

    auto* driver = static_cast<Engine*>(attempt->engine);
    auto engine = driver->get_engine();
    const auto& addr = attempt->remote;
    engine->whitelist_addr.push_back(addr);
    engine->whitelist_port.push_back(mqas::io::Ip::addr_get_port(addr));
    auto conn = engine->connect(addr, N_LSQVER,
        attempt->server_name.empty() ? nullptr : attempt->server_name.c_str()).lock();
    if (!conn) return EC_ConnectFailed;
    attempt->transport = conn;
    const auto handle = attempt->handle;
    {
        std::lock_guard lock(connect_mutex);
        connect_map.emplace(handle, conn);
    }
    if (auto owner = attempt->shared_engine.lock()) connection_engines[handle] = owner;
    std::weak_ptr<ConnectAttempt> weak = attempt;
    conn->on_close_signal.connect([weak](mqas::core::IConnect&) {
        if (auto state = weak.lock()) {
            if (!state->closing && !state->connected) state->close_reason = EC_ConnectFailed;
            finish_connection<Id, Config>(state);
        }
    });
    conn->set_connection_config(attempt->config);
    setup_base_stream_events(*conn, handle, attempt->context, [weak] {
        const auto state = weak.lock();
        return state && state->connected && !state->closing;
    });
    code = run<Connect::Registered, Id, Config>(*attempt);
    if (code != EC_Ok) return code;
    conn->on_hsk_done_signal.connect([weak](mqas::core::IConnect& connection, lsquic_hsk_status) {
        if (auto state = weak.lock()) complete_handshake<Id, Config>(state, connection);
    });
    if (attempt->handshake_timeout_ms) {
        attempt->timer = io_cxt->make_shared<mqas::io::Timer>();
        attempt->timer->start([weak](mqas::io::Timer*) {
            if (auto state = weak.lock(); state && !state->connected) close_connection<Id, Config>(state, EC_Timeout);
        }, attempt->handshake_timeout_ms, 0);
    }
    driver->process_conns();
    complete_handshake<Id, Config>(attempt, *conn);
    return EC_Ok;
}

template<size_t I, class Engines, class Config>
ErrorCode connect_internal(GSY_EngineId id, const char* path, const char* ip, short port,
    GSY_BaseConnectionContext* context, const GSY_ContextExtension* options, GSY_ConnectionHwnd& out) {
    if (id != I + 1) {
        if constexpr (I + 1 < std::tuple_size_v<Engines>)
            return connect_internal<I + 1, Engines, Config>(id, path, ip, port, context, options, out);
        return EC_EngineNotMatch;
    }
    if (!is_running) return EC_NotInitialized;
    if (!path || !*path || !context) return EC_Fail;
    if (options && std::is_void_v<Option<I + 1, Config>>) return EC_EngineNotMatch;
    auto attempt = std::make_shared<ConnectAttempt>();
    attempt->context = context;
    attempt->path = path;
    attempt->options = options;
    auto code = run<Connect::Validate, I + 1, Config>(*attempt);
    attempt->options = nullptr;
    if (code != EC_Ok) return code;
    const bool valid_remote = attempt->remote.storage.ss_family != 0 ||
        (ip && mqas::io::Ip::str2addr(ip, static_cast<uint16_t>(port), attempt->remote.storage));
    if (!ip && !valid_remote) return EC_InvalidAddress;
    const auto handle = get_new_connection_hwnd(id);
    if (!handle) return EC_ConnectOverLimit;
    attempt->handle = handle;
    if (!register_outbound_connection(handle, [attempt] {
        close_connection<I + 1, Config>(attempt, EC_Disconnected);
    })) {
        recycle_connection_hwnd(handle);
        return EC_NotInitialized;
    }
    out = handle;
    using Engine = std::tuple_element_t<I, Engines>;
    push_task([attempt, valid_remote] {
        if (attempt->closing) return;
        auto result = EC_InvalidAddress;
        if (valid_remote) {
            try { result = start_connection<Engine, I + 1, Config>(attempt); }
            catch (...) { result = EC_ConnectFailed; }
        }
        if (result != EC_Ok) close_connection<I + 1, Config>(attempt, result);
    }, true);
    return EC_Pending;
}

}

#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_CONNECTION(ET, CT) \
GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id, const char* path, \
    const char* ip, short port, GSY_BaseConnectionContext* context) { \
    GSY_ConnectionHwnd handle = InvalidConnection; \
    gsync::stages::connect_internal<0, ET, CT>(engine_id, path, ip, port, context, \
        context ? context->options : nullptr, handle); \
    return handle; \
}
