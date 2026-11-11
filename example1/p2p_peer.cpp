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
    GSY_PeerId self{};
    GSY_ConnectionHwnd connection{};
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
        recycle_connection_hwnd(p->connection);
    }
    // Once no connections remain, destruction cannot enter the nested I/O loop.
    p->engine.reset();
    p->timer.reset();
    p->socket.reset();
    if (!p->connected && p->credential)
        p2p_session_release_attempt(p->session_handle, p->credential);
    p->credential.reset();
    if (p->connected) {
        if (p->callbacks.on_closed) p->callbacks.on_closed(p->id, p->close_reason, p->callbacks.user_data);
    } else {
        if (p->callbacks.on_failed) p->callbacks.on_failed(p->id, p->close_reason, p->callbacks.user_data);
    }
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        if (p->connection != InvalidConnection) peer_connections.erase(p->connection);
        peers.erase(p->id);
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
    if (p->callbacks.on_connected) p->callbacks.on_connected(p->id, p->connection, p->callbacks.user_data);
}

void attach(const std::shared_ptr<Peer>& p, const std::shared_ptr<Connection>& conn) {
    if (!conn || p->done || p->connection != InvalidConnection) {
        if (conn) conn->close();
        if (!conn && !p->done) finish(p, EC_ConnectFailed);
        return;
    }
    const auto handle = get_new_connection_hwnd(2);
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
        if (auto peer = weak.lock(); peer && peer->connected && !peer->done && peer->callbacks.on_stream_open)
            peer->callbacks.on_stream_open(peer->id, handle,
                reinterpret_cast<GSY_StreamId>(stream->get_origin()), peer->callbacks.user_data);
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

ErrorCode GSY_StartP2PPeer(const char* config_file, GSY_PeerId self_id,
    const GSY_HelperResult* helper, const GSY_P2PPeerContext* context,
    GSY_P2PPeerHandle* out_peer) {
    if (out_peer) *out_peer = GSY_INVALID_P2P_PEER;
    if (!is_running) return EC_NotInitialized;
    if (!config_file || !*config_file || !helper || !context || !out_peer) return EC_Fail;
    if (helper->use_relay) return EC_NotSupported;
    if (!self_id || helper->ret != EC_Ok || !helper->peer_id ||
        (helper->is_server != 0 && helper->is_server != 1) ||
        !std::memchr(helper->peer_addr.ip, '\0', sizeof(helper->peer_addr.ip))) return EC_Fail;
    sockaddr address{};
    if (!helper->peer_addr.port || !mqas::io::Ip::str2addr_ipv4(helper->peer_addr.ip, helper->peer_addr.port, address))
        return EC_InvalidAddress;
    std::shared_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<const P2PSession> credential;
    const auto acquired = p2p_session_acquire(helper->session_handle, socket, credential);
    if (acquired != EC_Ok) return acquired;
    struct AttemptGuard {
        GSY_SessionHandle handle;
        std::shared_ptr<const P2PSession> session;
        bool transferred = false;
        ~AttemptGuard() { if (!transferred) p2p_session_release_attempt(handle, session); }
    } attempt{helper->session_handle, credential};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!credential || credential->self_id != self_id || credential->peer_id != helper->peer_id ||
        credential->is_server != (helper->is_server != 0) || credential->peer_ip != helper->peer_addr.ip ||
        credential->peer_port != helper->peer_addr.port || now < 0 ||
        static_cast<uint64_t>(now) < credential->issued_at_ms ||
        static_cast<uint64_t>(now) >= credential->expires_at_ms) return EC_Fail;
    auto p = std::make_shared<Peer>();
    p->callbacks = *context;
    p->self = self_id;
    p->socket = std::move(socket);
    p->credential = std::move(credential);
    p->session_handle = helper->session_handle;
    p->result = std::make_shared<Result>();
    p->result->set_peer_id(helper->peer_id);
    p->result->set_is_server(helper->is_server != 0);
    const bool server = helper->is_server != 0;
    const std::string config(config_file);
    std::lock_guard<std::mutex> lock(peers_mutex);
    if (!accepting_peers) return EC_NotInitialized;
    if (!next_peer) return EC_ConnectOverLimit;
    p->id = next_peer++;
    peers.emplace(p->id, p);
    *out_peer = p->id;
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
                attach(p, engine->connect(address, N_LSQVER).lock());
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
