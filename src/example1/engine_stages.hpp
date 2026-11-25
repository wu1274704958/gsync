#pragma once
#include "../core/template/engine_stages.hpp"
#include "gsync/example1/p2p_context.h"
#include "p2p_session.h"
#include "p2p_session_registry.h"
#include <mqas/comm/locator.h>
#include <mqas/tools/model/psk_tls_context.h>

namespace gsync::stages {
struct P2PConnectState {
    GSY_SessionHandle session;
    std::shared_ptr<const P2PSession> credential;

    ~P2PConnectState() {
        if (credential) p2p_session_release_attempt(session, credential);
    }
};

inline ErrorCode acquire_p2p_session(ConnectAttempt& attempt) {
    if (!attempt.options) return EC_Ok;
    if (attempt.options->type != GSY_CONTEXT_P2P_CONNECT ||
        attempt.options->size < sizeof(GSY_P2PConnectOptions)) return EC_Fail;
    const auto& options = *reinterpret_cast<const GSY_P2PConnectOptions*>(attempt.options);
    auto state = std::make_shared<P2PConnectState>();
    state->session = options.session;
    const auto code = p2p_session_acquire(state->session, attempt.socket, state->credential);
    if (code != EC_Ok) return code;
    const auto& credential = *state->credential;
    if (!credential.valid_now()) return EC_Timeout;
    if (credential.is_server) return EC_Fail;
    if (!credential.peer_port ||
        !mqas::io::Ip::str2addr_ipv4(credential.peer_ip.c_str(), credential.peer_port, attempt.remote))
        return EC_InvalidAddress;
    attempt.handshake_timeout_ms = 10000;
    attempt.extension = std::move(state);
    return EC_Ok;
}

inline ErrorCode prepare_p2p_connection(ConnectAttempt& attempt) {
    if (!attempt.extension) return EC_Ok;
    const auto& credential = *static_cast<P2PConnectState*>(attempt.extension.get())->credential;
    if (!credential.valid_now()) return EC_Timeout;
    const auto& alpn = toml::find<std::string>(*attempt.config, "engine_config", "alpn");
    attempt.tls = mqas::tools::p2p::make_psk_tls_context(false, credential.secret,
        credential.identity, credential.self_id, credential.peer_id, alpn);
    return attempt.tls ? EC_Ok : EC_ConnectFailed;
}

inline ErrorCode set_p2p_server_name(ConnectAttempt& attempt) {
    if (auto* state = static_cast<P2PConnectState*>(attempt.extension.get()))
        attempt.server_name = mqas::tools::p2p::psk_server_name(state->credential->identity);
    return EC_Ok;
}

inline ErrorCode register_p2p_identity(ConnectAttempt& attempt) {
    auto* state = static_cast<P2PConnectState*>(attempt.extension.get());
    if (!state) return EC_Ok;
    auto connection = attempt.transport.lock();
    if (!connection) return EC_Disconnected;
    auto peer = std::make_shared<mqas::tools::proto::p2p::NotifyConnectResult>();
    peer->set_peer_id(state->credential->peer_id);
    auto locator = mqas::comm::locator::inst();
    locator->deposit_cxt<LocalPeerId>(connection, state->credential->self_id);
    locator->deposit_cxt<decltype(peer)>(connection, std::move(peer));
    return EC_Ok;
}

inline ErrorCode accept_p2p_handshake(ConnectAttempt& attempt) {
    auto* state = static_cast<P2PConnectState*>(attempt.extension.get());
    if (!state) return EC_Ok;
    auto connection = attempt.transport.lock();
    if (!connection || connection->security_info().authentication != mqas::core::PeerAuthentication::ExternalPsk)
        return EC_ConnectFailed;
    if (!state->credential->valid_now()) return EC_Timeout;
    if (!p2p_session_consume(state->session, state->credential)) return EC_InvalidHandler;
    state->credential.reset();
    return EC_Ok;
}

inline ErrorCode clean_p2p_connection(ConnectAttempt& attempt) {
    if (!attempt.extension) return EC_Ok;
    if (auto connection = attempt.transport.lock()) mqas::comm::locator::inst()->clear_by_context(connection);
    attempt.extension.reset();
    return EC_Ok;
}
inline ErrorCode configure_p2p_server(ServerStart& frame) {
    if (!frame.context->options) return EC_Ok;
    if (frame.context->options->type != GSY_CONTEXT_P2P_SERVER ||
        frame.context->options->size < sizeof(GSY_P2PServerOptions)) return EC_Fail;
    const auto* option = reinterpret_cast<const GSY_P2PServerOptions*>(frame.context->options);
    frame.server->use_p2p = true;
    frame.server->socket = option->socket;
    frame.server->stream_policy = option->stream_policy;
    return EC_Ok;
}
inline ErrorCode prepare_server(ServerPrepare& frame) {
    if (!frame.server->use_p2p) return EC_Ok;
    frame.address_ready = true;
    frame.allow_shared_endpoint = true;
    return prepare_p2p_server(frame.server, frame.engine_id, frame.config, frame.address, frame.socket, frame.tls);
}
inline ErrorCode reuse_server(ServerReuse& frame) {
    if (bool(frame.owner->p2p) != frame.server->use_p2p) return EC_AlreadyExists;
    if (frame.server->use_p2p) frame.running.tls_context = nullptr;
    return EC_Ok;
}
inline ErrorCode attach_server(ServerEngineReady& frame) {
    if (!frame.server->use_p2p) return EC_Ok;
    return attach_p2p_server_engine(frame.server, frame.owner, frame.socket, frame.config);
}
inline ErrorCode authenticate_server(ServerIncoming& frame) {
    if (!frame.server->p2p) return EC_Ok;
    if (!authenticate_p2p_server(frame.server, frame.connection)) return EC_Fail;
    frame.notify_connected = true;
    return EC_Ok;
}
inline ErrorCode accepted_server(ServerIncoming& frame) {
    if (frame.server->p2p) attach_p2p_server_stream_policy(frame.server, frame.connection, frame.handle);
    return EC_Ok;
}
inline ErrorCode cleanup_server(ServerCleanup& frame) {
    stop_p2p_server(frame.server);
    return EC_Ok;
}
using ConnectConfig = Registry<Engine<2, GSY_P2PConnectOptions,
    StageList<Handler<Connect::Validate, acquire_p2p_session>,
        Handler<Connect::BeforeEngine, prepare_p2p_connection>,
        Handler<Connect::BeforeConnect, set_p2p_server_name>,
        Handler<Connect::Registered, register_p2p_identity>,
        Handler<Connect::Authenticated, accept_p2p_handshake>,
        Handler<Connect::Cleanup, clean_p2p_connection>>>>;
using ServerConfig = Registry<Engine<2, GSY_P2PServerOptions,
    StageList<Handler<Server::Validate, configure_p2p_server>,
        Handler<Server::BeforeEngine, prepare_server>, Handler<Server::Reuse, reuse_server>,
        Handler<Server::InitEngine, attach_server>, Handler<Server::Authenticate, authenticate_server>,
        Handler<Server::Accepted, accepted_server>, Handler<Server::Cleanup, cleanup_server>>>>;
}
