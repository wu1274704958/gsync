#include "lobby.h"
#include "lib.hpp"
#include "p2p_session.h"
#include "p2p_session_registry.h"
#include "../src/common.h"
#include <mqas/io/timer.h>
#include <mqas/tools/model/psk_tls_context.h>

namespace p2p = mqas::tools::p2p;

struct P2PServerState {
    struct Pending {
        GSY_SessionHandle handle;
        std::shared_ptr<const P2PSession> session;
        std::chrono::steady_clock::time_point deadline;
    };
    std::shared_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<mqas::io::Timer> timer;
    P2PDatagramEngine* engine = nullptr;
    std::string alpn;
    std::unordered_map<std::string, Pending> pending;

    bool refresh_tls() {
        std::vector<p2p::PskCredentialView> credentials;
        for (const auto& [name, entry] : pending) {
            const auto& c = *entry.session;
            credentials.push_back({c.secret, c.identity, c.self_id, c.peer_id});
        }
        auto tls = p2p::make_psk_server_context(credentials, alpn);
        if (!tls) return false;
        engine->set_tls_context(std::move(tls));
        return true;
    }
};

ErrorCode start_p2p_server(const std::shared_ptr<ServerEntry>& server, GSY_EngineId engine_id,
    const char* path, std::shared_ptr<const toml::value> config)
{
    if (engine_id != 2) return EC_EngineNotMatch;
    auto state = std::make_shared<P2PServerState>();
    state->socket = gsy_get_socket(server->ctx->socket);
    if (!state->socket) return EC_InvalidHandler;
    sockaddr address{};
    state->socket->get_sock_addr(address);
    if (address.sa_family != AF_INET) return EC_InvalidAddress;
    if (server_endpoint_overlaps(address)) return EC_AlreadyExists;
    state->alpn = toml::find<std::string>(*config, "engine_config", "alpn");
    auto tls = p2p::make_psk_server_context({}, state->alpn);
    if (!tls) return EC_ConnectFailed;
    auto engine = std::make_unique<P2PDatagramEngine>(*io_cxt);
    engine->init(*config, path, mqas::core::EngineFlags::Server, state->socket, std::move(tls));
    state->engine = engine.get();
    auto owner = std::make_shared<ServerEngineEntry>();
    owner->key = {engine_id, mqas::io::Ip::addr2str_ipv4(address), mqas::io::Ip::addr_get_port(address)};
    std::memcpy(&owner->bind_addr, &address, sizeof(sockaddr_in));
    owner->engine = engine.get();
    owner->servers.push_back(server);
    setup_server_connect_signals(engine->get_engine().get(), owner);
    state->timer = io_cxt->make_shared<mqas::io::Timer>();
    std::weak_ptr<ServerEntry> weak = server;
    state->timer->start([weak](mqas::io::Timer*) {
        auto owner = weak.lock();
        if (!owner || owner->stopping || !owner->p2p) return;
        auto& state = *owner->p2p;
        bool expired = false;
        for (auto it = state.pending.begin(); it != state.pending.end();) {
            if (it->second.session->valid_now() && std::chrono::steady_clock::now() < it->second.deadline) {
                ++it;
                continue;
            }
            p2p_session_release_attempt(it->second.handle, it->second.session);
            it = state.pending.erase(it);
            expired = true;
        }
        if (expired) {
            state.refresh_tls();
            if (owner->ctx->on_error) owner->ctx->on_error(EC_Timeout, owner->hwnd);
        }
    }, 250, 250);
    engine->start_recv();
    engine->process_conns();
    server_engines.emplace(owner->key, owner);
    server->config = std::move(config);
    server->owner = owner;
    server->p2p = std::move(state);
    engine.release();
    return EC_Ok;
}

void stop_p2p_server(const std::shared_ptr<ServerEntry>& server) {
    if (!server->p2p) return;
    auto state = std::exchange(server->p2p, {});
    if (state->timer->is_start()) state->timer->stop();
    for (const auto& [name, entry] : state->pending)
        p2p_session_release_attempt(entry.handle, entry.session);
}

bool authenticate_p2p_server(const std::shared_ptr<ServerEntry>& server,
    const std::shared_ptr<mqas::core::IConnect>& connection)
{
    const auto security = connection->security_info();
    if (security.handshake != mqas::core::HandshakeState::Succeeded ||
        security.authentication != mqas::core::PeerAuthentication::ExternalPsk) return false;
    // TLS accepts the name only when it matches the selected external PSK.
    const char* name = connection->get_sni();
    if (!name) return false;
    auto& state = *server->p2p;
    const auto it = state.pending.find(name);
    if (it == state.pending.end() || !it->second.session->valid_now() ||
        std::chrono::steady_clock::now() >= it->second.deadline) return false;
    const auto session = it->second.session;
    const auto handle = it->second.handle;
    if (!p2p_session_consume(handle, session)) return false;
    state.pending.erase(it);
    state.refresh_tls();
    auto result = std::make_shared<mqas::tools::proto::p2p::NotifyConnectResult>();
    result->set_peer_id(session->peer_id);
    auto locator = mqas::comm::locator::inst();
    locator->deposit_cxt<LocalPeerId>(connection, session->self_id);
    locator->deposit_cxt<decltype(result)>(connection, std::move(result));
    connection->on_close_signal.connect([](mqas::core::IConnect& closed) {
        mqas::comm::locator::inst()->clear_by_context(closed);
    });
    return true;
}

void attach_p2p_server_stream_policy(const std::shared_ptr<ServerEntry>& server,
    const std::shared_ptr<mqas::core::IConnect>& conn, GSY_ConnectionHwnd handle)
{
    std::weak_ptr<ServerEntry> weak = server;
    mqas::comm::locator::inst()->deposit_cxt<mqas::tools::p2p_direct::DirectStreamPolicy>(conn,
        [weak, handle](void* stream, GSY_P2PStreamId remote, GSY_P2PStreamId& local, std::string& reason) {
            const auto server = weak.lock();
            if (!server || server->stopping) { reason = "server is stopping"; return false; }
            if (!server->ctx->on_p2p_stream_request) return true;
            const char* text = nullptr;
            const auto accepted = server->ctx->on_p2p_stream_request(server->hwnd, handle,
                reinterpret_cast<GSY_StreamId>(stream), remote, &local, &text);
            if (text) reason = text;
            return accepted != 0 && !server->stopping;
        });
}

static std::shared_ptr<ServerEntry> find_p2p_server(GSY_ServerHwnd handle) {
    std::lock_guard lock(server_mutex);
    const auto it = server_map.find(handle);
    if (it == server_map.end() || it->second->stopping || !it->second->p2p) return {};
    return it->second;
}

ErrorCode GSY_RegisterServerP2PSession(GSY_ServerHwnd server, GSY_SessionHandle handle) {
    if (!is_running) return EC_NotInitialized;
    return push_task_with_result<ErrorCode>([server, handle] {
        const auto owner = find_p2p_server(server);
        if (!owner) return EC_InvalidHandler;
        std::shared_ptr<mqas::io::UdpSocket> socket;
        std::shared_ptr<const P2PSession> session;
        if (const auto error = p2p_session_acquire(handle, socket, session); error != EC_Ok) return error;
        auto& state = *owner->p2p;
        auto error = EC_Fail;
        if (!session->valid_now()) error = EC_Timeout;
        else if (session->is_server && state.socket == socket) {
            const auto name = p2p::psk_server_name(session->identity);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            const auto [it, inserted] = state.pending.emplace(name, P2PServerState::Pending{handle, session, deadline});
            if (!inserted) error = EC_AlreadyExists;
            else if (state.refresh_tls() && p2p_session_mark_server_registered(handle, session)) return EC_Ok;
            else { state.pending.erase(it); state.refresh_tls(); }
        }
        p2p_session_release_attempt(handle, session);
        return error;
    });
}

ErrorCode GSY_CancelServerP2PSession(GSY_ServerHwnd server, GSY_SessionHandle handle) {
    if (!is_running) return EC_NotInitialized;
    return push_task_with_result<ErrorCode>([server, handle] {
        const auto owner = find_p2p_server(server);
        if (!owner) return EC_InvalidHandler;
        auto& state = *owner->p2p;
        for (auto it = state.pending.begin(); it != state.pending.end(); ++it) {
            if (it->second.handle != handle) continue;
            p2p_session_release_attempt(handle, it->second.session);
            state.pending.erase(it);
            return state.refresh_tls() ? EC_Ok : EC_ConnectFailed;
        }
        return EC_InvalidHandler;
    });
}
