//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_COMMON_H
#define GSYNC_COMMON_H

#include <unordered_map>
#include <map>
#include <string>
#include <compare>
#include <toml.hpp>
#include <mutex>
#include <queue>
#include <atomic>
#include <vector>
#include <memory>
#include "core/server.h"
#include "def.h"
#include "uv.h"
#include "core/engine.h"
#include "mqas/context.h"

namespace mqas::core {
    class engine_base_interface;
    class IConnect;
}

constexpr GSY_ConnectionHwnd MAX_CONNECTION_HWND = 100000;

struct EngineKey {
    GSY_EngineId type;
    std::string bind_ip;
    uint16_t port;
    auto operator<=>(const EngineKey&) const = default;
};
struct EngineEntry {
    EngineKey key;
    sockaddr_storage bind_addr{};
    mqas::core::engine_base_interface* engine = nullptr;
};
extern std::map<EngineKey, std::shared_ptr<EngineEntry>> engine_map;
extern std::unordered_map<GSY_ConnectionHwnd, std::weak_ptr<EngineEntry>> connection_engines;
extern std::unordered_map<GSY_ConnectionHwnd,std::weak_ptr<mqas::core::IConnect>> connect_map;
extern std::mutex engines_mutex;
extern std::mutex connect_mutex;

extern std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
extern std::unique_ptr<mqas::io::Context> io_cxt;
extern GSY_Context* global_context;
extern std::atomic_bool is_running;

extern std::unique_ptr<std::thread> main_thread;
extern std::queue<std::function<void()>> task_queue;
extern std::mutex task_queue_mutex;
extern uv_async_t async_task_handle;

extern GSY_ConnectionHwnd get_new_connection_hwnd(GSY_EngineId);
extern void recycle_connection_hwnd(GSY_ConnectionHwnd);
extern void destroy_connect(GSY_ConnectionHwnd);
extern void cleanup_connection_engine(std::weak_ptr<EngineEntry>);
void p2p_session_remove_stream(GSY_ConnectionHwnd, GSY_StreamId);
void p2p_session_remove_connection(GSY_ConnectionHwnd);
void p2p_session_shutdown();
void p2p_peers_shutdown();
void p2p_peers_initialize();
namespace mqas::io { class UdpSocket; }
void gsy_socket_handles_shutdown();
std::shared_ptr<mqas::io::UdpSocket> gsy_get_socket(GSY_SocketHandle);
bool cancel_pending_p2p_connection(GSY_ConnectionHwnd);

// ── Server ────────────────────────────────────────────────────────────────────
#include "core/server.h"

// server_hwnd encoding:  engine_id * MAX_SERVER_HWND + local_id
// (mirrors the  engine_id * MAX_CONNECTION_HWND + local_id  pattern for connections)
constexpr GSY_ServerHwnd MAX_SERVER_HWND = 1000;

// SERVER_ENGINE_ID_OFFSET is added to server_hwnd when used as the engine_id
// namespace for connections accepted by that server, preventing overlap with
// client-produced conn_hwnds.
// Leave enough room for the server handle and its connection index in uint32_t.
constexpr GSY_EngineId SERVER_ENGINE_ID_OFFSET = 10000;

struct ServerEngineEntry;
struct P2PServerState;
struct ServerEntry {
    GSY_ServerHwnd hwnd = InvalidServer;
    GSY_BaseServerContext* ctx = nullptr;
    std::atomic_bool stopping{false};
    bool started = false;
    bool finalized = false;
    std::shared_ptr<const toml::value> config;
    std::weak_ptr<ServerEngineEntry> owner;
    std::shared_ptr<P2PServerState> p2p;
    std::unordered_map<GSY_ConnectionHwnd, std::weak_ptr<mqas::core::IConnect>> connections;
};
ErrorCode start_p2p_server(const std::shared_ptr<ServerEntry>&, GSY_EngineId,
    const char* path, std::shared_ptr<const toml::value>);
void stop_p2p_server(const std::shared_ptr<ServerEntry>&);
bool authenticate_p2p_server(const std::shared_ptr<ServerEntry>&, const std::shared_ptr<mqas::core::IConnect>&);
void attach_p2p_server_stream_policy(const std::shared_ptr<ServerEntry>&,
    const std::shared_ptr<mqas::core::IConnect>&, GSY_ConnectionHwnd);
struct ServerEngineEntry {
    EngineKey key;
    sockaddr_storage bind_addr{};
    mqas::core::engine_base_interface* engine = nullptr;
    std::vector<std::shared_ptr<ServerEntry>> servers;
};
extern std::map<EngineKey, std::shared_ptr<ServerEngineEntry>> server_engines;
extern std::unordered_map<GSY_ServerHwnd, std::shared_ptr<ServerEntry>> server_map;
extern std::mutex server_mutex;
extern bool server_shutting_down;
extern GSY_ServerHwnd get_new_server_hwnd(GSY_EngineId engine_id);
extern void recycle_server_hwnd(GSY_ServerHwnd);
extern bool server_endpoint_overlaps(const sockaddr&);
extern void finish_server_stop(const std::shared_ptr<ServerEntry>&);
extern void cleanup_server_engine(const std::shared_ptr<ServerEngineEntry>&);
extern void fail_server_start(const std::shared_ptr<ServerEntry>&, ErrorCode);
extern void shutdown_server_engines();
extern void wait_for_server_shutdown();

void push_task(const std::function<void()>&,bool force_delay = false);
template<typename R>
requires std::is_constructible_v<R>
R push_task_with_result(const std::function<R()> &task,bool force_delay = false);

#include "template/engine.hpp"
#include "template/connection.hpp"
#include "template/server.hpp"

#define GSY_DEFINE_ENGINE_TYPE(ET)                                                                                      \
GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_ENGINE(ET)                                                                              \
GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_CONNECTION(ET)                                                                          \
GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_SERVER(ET)                                                                              \

#endif //GSYNC_COMMON_H
