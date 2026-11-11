#include "../p2p_peer.h"
#include "../p2p_connection_datagram.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static std::atomic<GSY_ConnectionHwnd> lobby_connection{0};
static std::atomic<GSY_PeerId> self{0};
static std::atomic<GSY_PeerId> expected_peer{0};
static std::atomic<GSY_StreamId> lobby_stream{0};
static std::atomic<GSY_PeerId> incoming_request{0};
static std::atomic<bool> requested{false}, done{false}, failed{false};
static std::atomic<bool> authentication_failed{false};
static std::atomic<bool> expected_stop{false}, skipped_peer{false};
static std::atomic<int> terminal_callbacks{0};
static std::atomic<int> server_role{-1};
static std::atomic<GSY_P2PPeerHandle> peer_handle{0};
static std::atomic<GSY_SessionHandle> session_handle{0};
static std::atomic<GSY_ConnectionHwnd> peer_connection{0};
static std::atomic<bool> lobby_closed{false}, lobby_close_requested{false}, datagram_started{false};
static const char* peer_config = nullptr;
static GSY_P2PDatagramConnectionContext datagram{};

static void stream_created(ErrorCode code, GSY_StreamId, GSY_RequestId, void*) {
    if (code != EC_Ok) failed = true;
}
static void peer_connected(GSY_P2PPeerHandle, GSY_ConnectionHwnd conn, void*) {
    if (GSY_ReleaseP2PSession(session_handle) != EC_InvalidHandler) failed = true;
    if (std::getenv("GSYNC_TEST_BAD_IDENTITY")) { failed = true; return; }
    std::printf("peer connected role=%d conn=%u\n", server_role.load(), conn);
    peer_connection = conn;
    const char* scenario = std::getenv("GSYNC_TEST_PEER_SCENARIO");
    if (!server_role && (!scenario || std::strcmp(scenario, "lobby_close") != 0)) {
        datagram_started = true;
        if (GSY_ConnectPeerDatagram(conn, INVALID_PID, &datagram, 1, stream_created, nullptr) != EC_Pending)
            failed = true;
    }
}
static void peer_stream(GSY_P2PPeerHandle, GSY_ConnectionHwnd conn, GSY_StreamId sid, void*) {
    if (server_role && GSY_ServerConnectPeerDatagram(conn, sid, &datagram, 2) != EC_Ok) failed = true;
}
static void peer_failed(GSY_P2PPeerHandle, ErrorCode error, void*) {
    std::printf("peer failed code=%d\n", error);
    ++terminal_callbacks;
    if (GSY_ReleaseP2PSession(session_handle) != EC_Ok ||
        GSY_ReleaseP2PSession(session_handle) != EC_InvalidHandler) failed = true;
    if (std::getenv("GSYNC_TEST_BAD_IDENTITY")) {
        authentication_failed = !peer_connection && (error == EC_ConnectFailed || error == EC_Timeout || error == EC_Disconnected);
        if (!authentication_failed) failed = true;
        return;
    }
    const char* scenario = std::getenv("GSYNC_TEST_PEER_SCENARIO");
    if (scenario &&
        ((server_role && std::strcmp(scenario, "timeout") == 0 && error == EC_Timeout) ||
         (server_role && std::strcmp(scenario, "cancel") == 0 && error == EC_Disconnected) ||
         (!server_role && std::strcmp(scenario, "client_cancel") == 0 && error == EC_Disconnected) ||
         (server_role && std::strcmp(scenario, "bad_config") == 0 && error == EC_ConnectFailed))) {
        expected_stop = true;
        return;
    }
    failed = true;
}
static void peer_closed(GSY_P2PPeerHandle, ErrorCode error, void*) {
    std::printf("peer closed code=%d\n", error);
    ++terminal_callbacks;
}
static void on_helper(GSY_StreamId, GSY_HelperResult* result) {
    std::printf("helper code=%d role=%d peer=%u addr=%s:%u\n", result->ret,
        result->is_server, result->peer_id, result->peer_addr.ip, result->peer_addr.port);
    server_role = result->is_server;
    session_handle = result->session_handle;
    expected_peer = result->peer_id;
    GSY_P2PPeerContext context{};
    context.on_connected = peer_connected;
    context.on_stream_open = peer_stream;
    context.on_failed = peer_failed;
    context.on_closed = peer_closed;
    // Invalid handles and relay results must fail before creating a peer.
    auto invalid = *result;
    invalid.session_handle = GSY_INVALID_SESSION_HANDLE;
    GSY_P2PPeerHandle invalid_handle = 99;
    if (GSY_StartP2PPeer(peer_config, self, &invalid, &context, &invalid_handle) != EC_InvalidHandler || invalid_handle)
        failed = true;
    invalid = *result;
    invalid.use_relay = 1;
    if (GSY_StartP2PPeer(peer_config, self, &invalid, &context, &invalid_handle) != EC_NotSupported || invalid_handle)
        failed = true;
    invalid = *result;
    invalid.peer_id += 1000;
    if (GSY_StartP2PPeer(peer_config, self, &invalid, &context, &invalid_handle) != EC_Fail || invalid_handle)
        failed = true;
    invalid = *result;
    invalid.is_server = !invalid.is_server;
    if (GSY_StartP2PPeer(peer_config, self, &invalid, &context, &invalid_handle) != EC_Fail || invalid_handle)
        failed = true;
    invalid = *result;
    invalid.peer_addr.port = invalid.peer_addr.port == 65535 ? 1 : invalid.peer_addr.port + 1;
    if (GSY_StartP2PPeer(peer_config, self, &invalid, &context, &invalid_handle) != EC_Fail || invalid_handle)
        failed = true;
    const char* delayed = std::getenv("GSYNC_TEST_DELAY_PEER");
    if (delayed && std::strcmp(delayed, result->is_server ? "server" : "client") == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const char* scenario = std::getenv("GSYNC_TEST_PEER_SCENARIO");
    if (scenario && std::strcmp(scenario, "lobby_close") != 0 && std::strcmp(scenario, "parallel") != 0 &&
        (std::strcmp(scenario, "client_cancel") == 0 ? result->is_server : !result->is_server)) {
        skipped_peer = true;
        return;
    }
    GSY_P2PPeerHandle handle = 0;
    auto selected = *result;
    const auto start_self = std::getenv("GSYNC_TEST_BAD_IDENTITY") && !result->is_server ? self.load() + 1000 : self.load();
    const char* config = scenario && std::strcmp(scenario, "bad_config") == 0 ? "missing-p2p-config.toml" : peer_config;
    const auto code = GSY_StartP2PPeer(config, start_self, &selected, &context, &handle);
    if (std::getenv("GSYNC_TEST_BAD_IDENTITY") && !result->is_server) {
        authentication_failed = code == EC_Fail && handle == GSY_INVALID_P2P_PEER;
        if (GSY_ReleaseP2PSession(session_handle) != EC_Ok) failed = true;
        skipped_peer = true;
        if (!authentication_failed) failed = true;
        return;
    }
    peer_handle = handle;
    if (code != EC_Pending) { std::printf("start failed=%d\n", code); failed = true; }
    if (scenario && std::strcmp(scenario, "parallel") == 0 && code == EC_Pending) {
        GSY_P2PPeerHandle duplicate = 0;
        if (GSY_StartP2PPeer(peer_config, self, result, &context, &duplicate) != EC_SessionBusy || duplicate ||
            GSY_ReleaseP2PSession(session_handle) != EC_SessionBusy) failed = true;
    }
    if (scenario && (std::strcmp(scenario, "cancel") == 0 || std::strcmp(scenario, "client_cancel") == 0) && code == EC_Pending &&
        GSY_CloseP2PPeer(handle) != EC_Pending) failed = true;
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const bool initiator = std::strcmp(argv[1], "join") == 0;
    peer_config = argv[3];
    datagram.on_connected = [](ErrorCode code, GSY_StreamId sid, GSY_PeerId peer, const char*, GSY_RequestId) {
        if (code != EC_Ok || peer != expected_peer) { failed = true; return; }
        std::printf("datagram connected sid=%llu\n", (unsigned long long)sid);
        if (!server_role) GSY_SendDatagram(peer_connection, sid, "ping", 4, NONE_RID);
    };
    datagram.on_datagram_received = [](const uint8_t* data, size_t size, GSY_StreamId sid,
                                       GSY_PeerId peer, GSY_ConnectionHwnd conn) {
        if (peer != expected_peer) { failed = true; return; }
        if (server_role && size == 4 && std::memcmp(data, "ping", 4) == 0) {
            GSY_SendDatagram(conn, sid, "pong", 4, NONE_RID);
            done = true;
        } else if (!server_role && size == 4 && std::memcmp(data, "pong", 4) == 0) done = true;
    };
    datagram.on_connect_failed = [](ErrorCode, GSY_StreamId, GSY_PeerId, GSY_RequestId) {
        authentication_failed = true;
        if (!std::getenv("GSYNC_TEST_BAD_IDENTITY")) failed = true;
    };
    GSY_Context ctx{};
    if (GSY_initialize(0, &ctx) != EC_Ok) return 3;
    GSY_BaseConnectionContext connection{};
    connection.on_connect = [](int code, GSY_ConnectionHwnd handle) { std::printf("lobby connect code=%d handle=%u\n", code, handle); if (code == EC_Ok) lobby_connection = handle; else failed = true; };
    connection.on_error = [](int code, GSY_ConnectionHwnd handle) { std::printf("connection error=%d handle=%u\n", code, handle); failed = true; };
    connection.on_disconnect = [](int, GSY_ConnectionHwnd) { lobby_closed = true; };
    GSY_LobbyStreamContext lobby{};
    lobby.on_registration_success = [](ErrorCode code, GSY_StreamId sid, GSY_PeerId id) {
        std::printf("registered code=%d sid=%llu id=%u\n", code, (unsigned long long)sid, id);
        if (code != EC_Ok) failed = true; else { self = id; lobby_stream = sid; }
    };
    lobby.on_peer_req_connect = [](GSY_PeerData* other, GSY_StreamId sid) {
        std::printf("peer request id=%u\n", other ? other->peer_id : 0);
        if (other) incoming_request = other->peer_id;
    };
    lobby.on_receive_peer_list = [](GSY_PeerData* list, uint32_t size, GSY_StreamId sid) {
        std::printf("peer list size=%u\n", size);
        for (uint32_t i = 0; i < size; ++i) if (list[i].peer_id != self && !requested.exchange(true))
            GSY_RequestConnectPeer(lobby_connection, sid, list[i].peer_id, NONE_RID);
    };
    lobby.on_helper_quit_result = on_helper;
    lobby.on_error = [](GSY_ConnectionHwnd, GSY_StreamId, ErrorCode error, const char*, GSY_RequestId) {
        std::printf("lobby error=%d\n", error); failed = true;
    };
    GSY_connect(1, argv[2], "127.0.0.1", 8101, &connection);
    for (int i = 0; i < 500 && !lobby_connection && !failed; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (lobby_connection) {
        auto sid = GSY_RegisterToLobby(lobby_connection, initiator ? "join" : "host", "", &lobby);
        std::printf("register stream=%llu\n", (unsigned long long)sid);
    }
    int skipped_ticks = 0;
    const char* scenario = std::getenv("GSYNC_TEST_PEER_SCENARIO");
    const bool lobby_close = scenario && std::strcmp(scenario, "lobby_close") == 0;
    const bool parallel = scenario && std::strcmp(scenario, "parallel") == 0;
    for (int i = 0; i < 1500 && !done && !failed && !authentication_failed && !expected_stop; ++i) {
        if (lobby_close && peer_connection && !lobby_close_requested.exchange(true))
            GSY_disconnect(lobby_connection);
        if (lobby_close && lobby_closed && peer_connection && !server_role && !datagram_started.exchange(true)) {
            if (GSY_ConnectPeerDatagram(peer_connection, INVALID_PID, &datagram, 1, stream_created, nullptr) != EC_Pending)
                failed = true;
        }
        if (skipped_peer && ++skipped_ticks >= (scenario && std::strcmp(scenario, "timeout") == 0 ? 1150 : 150)) break;
        const auto incoming = incoming_request.exchange(0);
        if (incoming) GSY_RespondPeerConnectRequest(lobby_connection, lobby_stream, incoming, true, NONE_RID);
        if (initiator && lobby_stream && !requested && i % 25 == 0)
            GSY_FetchPeerList(lobby_connection, lobby_stream, NONE_RID);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool exchange_ok = scenario && !lobby_close && !parallel ? (expected_stop || skipped_peer) && !failed :
        std::getenv("GSYNC_TEST_BAD_IDENTITY") ? authentication_failed && !failed : done && !failed;
    if (exchange_ok) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (peer_handle) GSY_CloseP2PPeer(peer_handle);
    GSY_terminate();
    const bool success = exchange_ok && !failed && terminal_callbacks == (skipped_peer ? 0 : 1);
    std::printf("peer result=%s lobby=%u self=%u stream=%llu requested=%d failed=%d terminal=%d\n", success ? "PASS" : "FAIL",
        lobby_connection.load(), self.load(), (unsigned long long)lobby_stream.load(), requested.load(), failed.load(), terminal_callbacks.load());
    return success ? 0 : 1;
}
