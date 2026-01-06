//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_COMMON_H
#define GSYNC_COMMON_H

#include <unordered_map>
#include <mutex>
#include "def.h"
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
extern std::unique_ptr<std::thread> main_thread;

extern std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
extern std::unique_ptr<mqas::io::Context> io_cxt;
extern GSY_Context* global_context;
extern std::atomic_bool is_running;

extern GSY_ConnectionHwnd get_new_connection_hwnd(GSY_EngineId);
extern void recycle_connection_hwnd(GSY_ConnectionHwnd);
extern void destroy_connect(GSY_ConnectionHwnd);
extern bool destroy_engine_by_conn_hwnd(GSY_ConnectionHwnd);

extern void push_task(const std::function<void()>&);

#include "template/engine.hpp"
#include "template/connection.hpp"

#define GSY_DEFINE_ENGINE_TYPE(ET)                                                                                      \
GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_ENGINE(ET)                                                                              \
GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_CONNECTION(ET)                                                                          \

#endif //GSYNC_COMMON_H