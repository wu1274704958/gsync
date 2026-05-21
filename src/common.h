//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_COMMON_H
#define GSYNC_COMMON_H

#include <unordered_map>
#include <mutex>
#include <queue>
#include "def.h"
#include "uv.h"
#include "core/engine.h"
#include "mqas/context.h"

namespace mqas::core {
    class engine_base_interface;
    class IConnect;
}

constexpr GSY_ConnectionHwnd MAX_CONNECTION_HWND = 100000;

extern std::unordered_map<GSY_EngineId,mqas::core::engine_base_interface*> engine_map;
extern std::unordered_map<GSY_ConnectionHwnd,std::weak_ptr<mqas::core::IConnect>> connect_map;
extern std::mutex engines_mutex;
extern std::mutex connect_mutex;

extern std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
extern std::unique_ptr<mqas::io::Context> io_cxt;
extern GSY_Context* global_context;
extern std::atomic_bool is_running;

extern std::unique_ptr<std::thread> main_thread;
extern std::queue<std::function<void()>> task_queue;
extern std::atomic_bool task_queue_push;
extern std::atomic_bool task_queue_running;
extern uv_async_t async_task_handle;

extern GSY_ConnectionHwnd get_new_connection_hwnd(GSY_EngineId);
extern void recycle_connection_hwnd(GSY_ConnectionHwnd);
extern void destroy_connect(GSY_ConnectionHwnd);
extern bool destroy_engine_by_conn_hwnd(GSY_ConnectionHwnd);

// ── Server ────────────────────────────────────────────────────────────────────
#include "core/server.h"

// server_hwnd encoding:  engine_id * MAX_SERVER_HWND + local_id
// (mirrors the  engine_id * MAX_CONNECTION_HWND + local_id  pattern for connections)
constexpr GSY_ServerHwnd MAX_SERVER_HWND = 1000;

// SERVER_ENGINE_ID_OFFSET is added to server_hwnd when used as the engine_id
// namespace for connections accepted by that server, preventing overlap with
// client-produced conn_hwnds.
constexpr GSY_EngineId SERVER_ENGINE_ID_OFFSET = 50000;

extern std::unordered_map<GSY_ServerHwnd, struct ServerEntry> server_map;
extern std::mutex server_mutex;
extern GSY_ServerHwnd get_new_server_hwnd(GSY_EngineId engine_id);
extern void recycle_server_hwnd(GSY_ServerHwnd);

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