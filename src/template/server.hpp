#pragma once
#include <type_traits>
#include "def.h"
#include "../common.h"
#include "core/server.h"
#include "mqas/core/engine.h"
#include "mqas/core/engine_driver.h"

// forward-declared in server.cpp
extern void register_server_entry(GSY_ServerHwnd,
                                   mqas::core::engine_base_interface*,
                                   GSY_BaseServerContext*);

// ─── setup_server_connect_signals ────────────────────────────────────────────
// Typed helper: deduces C from mqas::core::engine<C>* so the on_new_connect_signal
// lambda receives std::shared_ptr<C> (the concrete Connect type) and therefore has
// access to on_new_stream_signal and on_stream_close_signal declared on Connect<S>.
template<typename C>
void setup_server_connect_signals(mqas::core::engine<C>* inner_engine,
                                   GSY_ServerHwnd hwnd,
                                   GSY_BaseServerContext* server_ctx)
{
    inner_engine->on_new_connect_signal.connect(
        [hwnd, server_ctx](std::shared_ptr<C> conn)
        {
            // SERVER_ENGINE_ID_OFFSET 保证 server 产生的 conn_hwnd 与 client 不冲突
            const GSY_ConnectionHwnd conn_hwnd =
                get_new_connection_hwnd(hwnd + SERVER_ENGINE_ID_OFFSET);
            if (conn_hwnd == InvalidConnection)
            {
                conn->close();
                if (server_ctx->on_error)
                    server_ctx->on_error(EC_ConnectOverLimit, hwnd);
                return;
            }

            // ① 向业务层索取该连接专属 context
            GSY_BaseConnectionContext* conn_ctx =
                server_ctx->on_client_connect
                    ? server_ctx->on_client_connect(hwnd, conn_hwnd)
                    : nullptr;

            if (!conn_ctx)
            {
                // 业务层拒绝 → 关闭连接并回收 handle
                recycle_connection_hwnd(conn_hwnd);
                conn->close();
                return;
            }

            // ② 注册到 connect_map（加锁）
            {
                std::lock_guard<std::mutex> lock(connect_mutex);
                connect_map.insert({conn_hwnd, conn});
            }

            conn->set_cxt(conn_ctx);

            // ③ 与 handle_new_connection_internal 完全对称的信号连接
            conn->on_close_signal.connect([hwnd, conn_hwnd, server_ctx](mqas::core::IConnect&)
            {
                destroy_connect(conn_hwnd);
                if (server_ctx->on_client_disconnect)
                    server_ctx->on_client_disconnect(hwnd, conn_hwnd);
            });

            conn->on_hsk_done_signal.connect(
                [conn_hwnd, conn_ctx](mqas::core::IConnect&, ::lsquic_hsk_status status)
            {
                if (status == ::lsquic_hsk_status::LSQ_HSK_OK)
                {
                    if (conn_ctx->on_connect)
                        conn_ctx->on_connect(EC_Ok, conn_hwnd);
                }
            });

            if (conn_ctx->on_stream_open)
            {
                conn->on_new_stream_signal.connect(
                    [conn_hwnd, conn_ctx](std::shared_ptr<mqas::core::IStream> stream)
                {
                    conn_ctx->on_stream_open(
                        conn_hwnd,
                        reinterpret_cast<GSY_StreamId>(stream->get_origin()),
                        EC_Ok);
                });
            }

            if (conn_ctx->on_stream_close)
            {
                conn->on_stream_close_signal.connect(
                    [conn_hwnd, conn_ctx](std::shared_ptr<mqas::core::IStream> stream)
                {
                    conn_ctx->on_stream_close(
                        conn_hwnd,
                        reinterpret_cast<GSY_StreamId>(stream->get_origin()),
                        EC_Ok);
                });
            }
        });
}

// ─── handle_new_server_internal ─────────────────────────────────────────────
// Must be called from inside push_task — all lsquic calls are on the main loop thread.
template<typename ET>
requires std::is_base_of_v<mqas::core::engine_base_interface, ET>
void handle_new_server_internal(GSY_ServerHwnd hwnd,
                                 ET* engine,
                                 const std::string& config_file,
                                 GSY_BaseServerContext* server_ctx)
{
    // ── 1. init / start (lsquic calls — inside push_task ✓) ──────────────
    engine->init(config_file.c_str(), mqas::core::EngineFlags::Server);
    engine->start_recv();
    engine->process_conns();

    // ── 2. 注册到 server_map（加锁，通过 register_server_entry）────────────
    register_server_entry(hwnd, engine, server_ctx);

    // ── 3. 监听新连接 ────────────────────────────────────────────────────
    // engine->get_engine() returns shared_ptr<engine<C>>; pass raw ptr so
    // setup_server_connect_signals can deduce C and type the lambda correctly.
    setup_server_connect_signals(engine->get_engine().get(), hwnd, server_ctx);

    // ── 4. 通知业务层服务器已就绪 ─────────────────────────────────────────
    if (server_ctx->on_started)
        server_ctx->on_started(EC_Ok, hwnd);
}

// ─── launch_server_internal ──────────────────────────────────────────────────
// Template recursion identical in structure to connect_internal.
// engine_id - 1 == I  → matched EngineType: allocate hwnd, push_task to init.
// otherwise           → recurse to I+1.
template<size_t I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ServerHwnd launch_server_internal(GSY_EngineId engine_id,
                                       const char* config_file,
                                       GSY_BaseServerContext* server_ctx)
{
    if (engine_id - 1 == I)
    {
        using EngineType = std::tuple_element_t<I, TU>;

        // Allocate handle: encodes engine_id so callers can identify the engine at a glance
        // server_hwnd = engine_id * MAX_SERVER_HWND + local_id
        const GSY_ServerHwnd hwnd = get_new_server_hwnd(engine_id);
        if (hwnd == InvalidServer)
        {
            if (server_ctx->on_error)
                server_ctx->on_error(EC_ConnectOverLimit, InvalidServer);
            return InvalidServer;
        }

        // All engine init and lsquic calls go through push_task ✓
        push_task([hwnd,
                   config = std::string(config_file),
                   server_ctx]()
        {
            auto* engine = new EngineType(*io_cxt);
            handle_new_server_internal(hwnd, engine, config, server_ctx);
        });

        return hwnd;
    }
    else
    {
        if constexpr (I + 1 >= std::tuple_size_v<TU>)
        {
            if (server_ctx->on_error)
                server_ctx->on_error(EC_EngineNotMatch, InvalidServer);
            return InvalidServer;
        }
        else
        {
            return launch_server_internal<I + 1, TU>(engine_id, config_file, server_ctx);
        }
    }
}

// ─── Macro ───────────────────────────────────────────────────────────────────
#define GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_SERVER(ET)                                          \
GSY_ServerHwnd GSYNC_EXTERN GSY_launch_server(GSY_EngineId engine_id,                      \
                                               const char* config_file,                     \
                                               GSY_BaseServerContext* cxt)                  \
{                                                                                           \
    return launch_server_internal<0, ET>(engine_id, config_file, cxt);                     \
}                                                                                           \
