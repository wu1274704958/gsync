#include "p2p_peer.h"
#include "p2p_peer_internal.h"
#include "p2p_session_registry.h"
#include "p2p_session.h"
#include "lib.hpp"
#include "../src/common.h"
#include <mqas/comm/locator.h>
#include <mqas/io/timer.h>
#include <mqas/io/ip.h>
#include <mqas/tools/model/psk_tls_context.h>
#include <toml.hpp>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <string>

namespace {
using Connection = mqas::core::Connect<mqas::core::StreamVariant<P2PDatagramStreamPair>>;
using Result = mqas::tools::proto::p2p::NotifyConnectResult;
struct Peer {
    GSY_P2PPeerHandle id{};
    GSY_P2PPeerContext callbacks{};
    GSY_BaseConnectionContext* base = nullptr;
    GSY_PeerId self{};
    GSY_ConnectionHwnd connection{};
    GSY_ConnectionHwnd reserved{};
    std::shared_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<P2PDatagramEngine> engine;
    std::shared_ptr<mqas::io::Timer> timer;
    std::shared_ptr<Result> result;
    std::shared_ptr<const P2PSession> credential;
    GSY_SessionHandle session_handle = GSY_INVALID_SESSION_HANDLE;
    std::weak_ptr<Connection> transport;
    bool connected = false;
    bool done = false;
    bool cleaned = false;
    ErrorCode close_reason = EC_Disconnected;
};
std::mutex peers_mutex;
std::condition_variable peers_empty;
bool accepting_peers = false;
std::unordered_map<GSY_P2PPeerHandle, std::shared_ptr<Peer>> peers;
std::unordered_map<GSY_ConnectionHwnd, std::weak_ptr<Peer>> peer_connections;
uint64_t next_peer = 1;

void cleanup(const std::shared_ptr<Peer>& p) {
    if (!p->done || p->cleaned) return;
    if (p->engine && p->engine->get_engine() && p->engine->get_engine()->connect_count() != 0) {
        p->engine->process_conns();
        if (p->engine->get_engine()->connect_count() != 0) return;
    }
    p->cleaned = true;
    if (auto c = p->transport.lock()) mqas::comm::locator::inst()->clear_by_context(c);
    if (p->connection != InvalidConnection) {
        { std::lock_guard<std::mutex> lock(connect_mutex); connect_map.erase(p->connection); }

    }
    // Once no connections remain, destruction cannot enter the nested I/O loop.
    p->engine.reset();
    p->timer.reset();
    p->socket.reset();
    if (!p->connected && p->credential)
        p2p_session_release_attempt(p->session_handle, p->credential);
    p->credential.reset();
    const auto handle = p->connection ? p->connection : p->reserved;
    {
        std::lock_guard lock(peers_mutex);
        if (handle) peer_connections.erase(handle);
    }
    if (p->base) {
        auto callback = p->connected ? p->base->on_disconnect : p->base->on_error;
        if (callback) callback(p->close_reason, handle);
    } else if (p->connected) {
        if (p->callbacks.on_closed) p->callbacks.on_closed(p->id, p->close_reason, p->callbacks.user_data);
    } else {
        if (p->callbacks.on_failed) p->callbacks.on_failed(p->id, p->close_reason, p->callbacks.user_data);
    }
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        peers.erase(p->id);
        if (handle) recycle_connection_hwnd(handle);
    }
    peers_empty.notify_all();
}

void finish(const std::shared_ptr<Peer>& p, ErrorCode error) {
    if (p->done) return;
    p->done = true;
    p->close_reason = error;
    if (p->timer && p->timer->is_start()) p->timer->stop();
    if (auto c = p->transport.lock(); c && !c->is_closed()) c->close();
    // Destroy only after the lsquic callback stack has unwound.
    push_task([p] { cleanup(p); }, true);
}

void notify_authenticated(const std::shared_ptr<Peer>& p, mqas::core::IConnect& conn) {
    if (p->done || p->connected) return;
    const auto security = conn.security_info();
    if (security.handshake == mqas::core::HandshakeState::Pending) return;
    if (security.handshake != mqas::core::HandshakeState::Succeeded ||
        security.authentication != mqas::core::PeerAuthentication::ExternalPsk) {
        finish(p, EC_ConnectFailed);
        return;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < 0 || static_cast<uint64_t>(now) < p->credential->issued_at_ms ||
        static_cast<uint64_t>(now) >= p->credential->expires_at_ms) { finish(p, EC_ConnectFailed); return; }
    if (!p2p_session_consume(p->session_handle, p->credential)) { finish(p, EC_InvalidHandler); return; }
    p->credential.reset();
    if (p->timer && p->timer->is_start()) p->timer->stop();
    p->connected = true;
    if (p->base && p->base->on_connect) p->base->on_connect(EC_Ok, p->connection);
    if (p->callbacks.on_connected) p->callbacks.on_connected(p->id, p->connection, p->callbacks.user_data);
}

void attach(const std::shared_ptr<Peer>& p, const std::shared_ptr<Connection>& conn) {
    if (!conn || p->done || p->connection != InvalidConnection) {
        if (conn) conn->close();
        if (!conn && !p->done) finish(p, EC_ConnectFailed);
        return;
    }
    const auto handle = p->reserved ? p->reserved : get_new_connection_hwnd(2);
    if (handle == InvalidConnection) { conn->close(); finish(p, EC_ConnectOverLimit); return; }
    p->connection = handle;
    p->transport = conn;
    { std::lock_guard<std::mutex> lock(peers_mutex); peer_connections.emplace(handle, p); }
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        connect_map.emplace(handle, conn);
    }
    auto locator = mqas::comm::locator::inst();
    locator->deposit_cxt<LocalPeerId>(conn, p->self);
    locator->deposit_cxt<std::shared_ptr<Result>>(conn, p->result);
    std::weak_ptr<Peer> weak = p;
    conn->on_new_stream_signal.connect([weak, handle](std::shared_ptr<Connection::StreamType> stream) {
        const auto peer = weak.lock();
        if (!peer || !peer->connected || peer->done) return;
        const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
        if (peer->base && peer->base->on_stream_open) peer->base->on_stream_open(handle, sid, EC_Ok);
        if (peer->callbacks.on_stream_open)
            peer->callbacks.on_stream_open(peer->id, handle, sid, peer->callbacks.user_data);
    });
    conn->on_stream_close_signal.connect([weak, handle](std::shared_ptr<Connection::StreamType> stream) {
        const auto peer = weak.lock();
        if (peer && peer->connected && peer->base && peer->base->on_stream_close)
            peer->base->on_stream_close(handle, reinterpret_cast<GSY_StreamId>(stream->get_origin()), EC_Ok);
    });
    conn->on_hsk_done_signal.connect([weak](mqas::core::IConnect& connection, lsquic_hsk_status) {
        if (auto peer = weak.lock()) notify_authenticated(peer, connection);
    });
    conn->on_close_signal.connect([weak](mqas::core::IConnect& c) {
        mqas::comm::locator::inst()->clear_by_context(c);
        if (auto peer = weak.lock()) {
            if (!peer->done) finish(peer, EC_Disconnected);
            else push_task([peer] { cleanup(peer); }, true);
        }
    });
    // Handles server promotion and a client handshake completed before attachment.
    notify_authenticated(p, *conn);
}
}

bool p2p_peer_credentials(GSY_ConnectionHwnd handle, P2PCredentials& out) {
    std::lock_guard<std::mutex> lock(peers_mutex);
    const auto it = peer_connections.find(handle);
    if (it == peer_connections.end()) return false;
    auto peer = it->second.lock();
    if (!peer) return false;
    out.self = peer->self;
    out.other = peer->result->peer_id();
    return true;
}

bool cancel_pending_p2p_connection(GSY_ConnectionHwnd handle) {
    std::lock_guard lock(peers_mutex);
    const auto it = peer_connections.find(handle);
    if (it == peer_connections.end()) return false;
    auto peer = it->second.lock();
    if (!peer || !peer->base) return false;
    push_task([peer] { finish(peer, EC_Disconnected); }, true);
    return true;
}

static ErrorCode start_peer(const char* config_file, GSY_SessionHandle session_handle,
    GSY_PeerId self_id, const GSY_HelperResult* helper, const GSY_P2PPeerContext* context,
    GSY_P2PPeerHandle* out_peer, GSY_BaseConnectionContext* base, GSY_ConnectionHwnd* out_connection) {
    std::shared_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<const P2PSession> credential;
    const auto acquired = p2p_session_acquire(session_handle, socket, credential);
    if (acquired != EC_Ok) return acquired;
    struct AttemptGuard {
        GSY_SessionHandle handle;
        std::shared_ptr<const P2PSession> session;
        bool transferred = false;
        ~AttemptGuard() { if (!transferred) p2p_session_release_attempt(handle, session); }
    } attempt{session_handle, credential};
    if (!credential->valid_now()) return helper ? EC_Fail : EC_Timeout;
    if (helper && (credential->self_id != self_id || credential->peer_id != helper->peer_id ||
        credential->is_server != (helper->is_server != 0) || credential->peer_ip != helper->peer_addr.ip ||
        credential->peer_port != helper->peer_addr.port)) return EC_Fail;
    if (base && credential->is_server) return EC_Fail;
    sockaddr address{};
    if (!credential->peer_port ||
        !mqas::io::Ip::str2addr_ipv4(credential->peer_ip.c_str(), credential->peer_port, address))
        return EC_InvalidAddress;
    auto p = std::make_shared<Peer>();
    if (context) p->callbacks = *context;
    p->base = base;
    p->self = credential->self_id;
    p->socket = std::move(socket);
    p->credential = std::move(credential);
    p->session_handle = session_handle;
    p->result = std::make_shared<Result>();
    p->result->set_peer_id(p->credential->peer_id);
    const bool server = p->credential->is_server;
    p->result->set_is_server(server);
    const std::string config(config_file);
    std::lock_guard<std::mutex> lock(peers_mutex);
    if (!accepting_peers) return EC_NotInitialized;
    if (!next_peer) return EC_ConnectOverLimit;
    if (base) {
        p->reserved = get_new_connection_hwnd(2);
        if (!p->reserved) return EC_ConnectOverLimit;
        peer_connections.emplace(p->reserved, p);
        *out_connection = p->reserved;
    }
    p->id = next_peer++;
    peers.emplace(p->id, p);
    if (out_peer) *out_peer = p->id;
    attempt.transferred = true;
    push_task([p, config, address, server] {
        if (p->done) return;
        try {
            p->engine = std::make_shared<P2PDatagramEngine>(*io_cxt);
            const auto conf = toml::parse(config);
            const auto alpn = toml::find<std::string>(conf, "engine_config", "alpn");
            if (!mqas::tools::p2p::init_psk_peer_engine(*p->engine, config.c_str(), server, p->socket,
                p->credential->secret, p->credential->identity, p->self, p->credential->peer_id, alpn)) {
                finish(p, EC_ConnectFailed);
                return;
            }
            auto engine = p->engine->get_engine();
            engine->whitelist_addr.push_back(address);
            engine->whitelist_port.push_back(mqas::io::Ip::addr_get_port(address));
            std::weak_ptr<Peer> cleanup_weak = p;
            engine->on_connect_closed_signal.connect([cleanup_weak](std::shared_ptr<Connection>) {
                if (auto peer = cleanup_weak.lock(); peer && peer->done)
                    push_task([peer] { cleanup(peer); }, true);
            });
            if (server) {
                std::weak_ptr<Peer> weak = p;
                engine->on_new_connect_signal.connect([weak](std::shared_ptr<Connection> c) {
                    if (auto peer = weak.lock()) attach(peer, c); else if (c) c->close();
                });
            }
            p->engine->start_recv();
            p->engine->process_conns();
            if (!server) {
                const auto name = p->base ? mqas::tools::p2p::psk_server_name(p->credential->identity) : std::string{};
                attach(p, engine->connect(address, N_LSQVER, p->base ? name.c_str() : nullptr).lock());
            }
            if (!p->connected && !p->done) {
                p->timer = io_cxt->make_shared<mqas::io::Timer>();
                std::weak_ptr<Peer> weak = p;
                p->timer->start([weak](mqas::io::Timer*) {
                    push_task([weak] { if (auto peer = weak.lock()) finish(peer, EC_Timeout); }, true);
                }, 10000, 0);
            }
        } catch (...) { finish(p, EC_ConnectFailed); }
    }, true);
    return EC_Pending;
}

ErrorCode GSY_StartP2PPeer(const char* config_file, GSY_PeerId self_id,
    const GSY_HelperResult* helper, const GSY_P2PPeerContext* context, GSY_P2PPeerHandle* out_peer) {
    if (out_peer) *out_peer = GSY_INVALID_P2P_PEER;
    if (!is_running) return EC_NotInitialized;
    if (!config_file || !*config_file || !helper || !context || !out_peer) return EC_Fail;
    if (helper->use_relay) return EC_NotSupported;
    if (!self_id || helper->ret != EC_Ok || !helper->peer_id ||
        (helper->is_server != 0 && helper->is_server != 1) ||
        !std::memchr(helper->peer_addr.ip, '\0', sizeof(helper->peer_addr.ip))) return EC_Fail;
    return start_peer(config_file, helper->session_handle, self_id, helper, context, out_peer, nullptr, nullptr);
}

ErrorCode GSY_connect_session(const char* config_file, GSY_SessionHandle session,
    GSY_BaseConnectionContext* context, GSY_ConnectionHwnd* out_connection) {
    if (out_connection) *out_connection = InvalidConnection;
    if (!is_running) return EC_NotInitialized;
    if (!config_file || !*config_file || !context || !out_connection) return EC_Fail;
    return start_peer(config_file, session, 0, nullptr, nullptr, nullptr, context, out_connection);
}

ErrorCode GSY_CloseP2PPeer(GSY_P2PPeerHandle id) {
    std::lock_guard<std::mutex> lock(peers_mutex);
    if (!accepting_peers) return EC_NotInitialized;
    const auto it = peers.find(id);
    if (it == peers.end()) return EC_InvalidHandler;
    push_task([peer = it->second] { finish(peer, EC_Disconnected); }, true);
    return EC_Pending;
}

void p2p_peers_initialize() {
    std::lock_guard<std::mutex> lock(peers_mutex);
    accepting_peers = true;
}

void p2p_peers_shutdown() {
    { std::lock_guard<std::mutex> lock(peers_mutex); accepting_peers = false; }
    push_task_with_result<int>([] {
        std::vector<std::shared_ptr<Peer>> copy;
        { std::lock_guard<std::mutex> lock(peers_mutex); for (const auto& [id, peer] : peers) copy.push_back(peer); }
        for (auto& peer : copy) finish(peer, EC_Disconnected);
        return 0;
    });
    std::unique_lock<std::mutex> lock(peers_mutex);
    peers_empty.wait(lock, [] { return peers.empty(); });
}
