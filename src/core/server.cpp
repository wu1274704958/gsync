#include "core/server.h"
#include "mqas/tools/unique_id_generator.h"
#include "../common.h"

// ─── server entry ─────────────────────────────────────────────────────────────
struct ServerEntry
{
    mqas::core::engine_base_interface* engine = nullptr;
    GSY_BaseServerContext*             ctx    = nullptr;
};

// ─── server_map & synchronization ────────────────────────────────────────────
std::unordered_map<GSY_ServerHwnd, ServerEntry> server_map;
std::mutex server_mutex;

static mqas::tools::unique_id_generator<GSY_ServerHwnd, true> server_id_generator;

// ─── handle helpers ──────────────────────────────────────────────────────────
// server_hwnd encoding:  engine_id * MAX_SERVER_HWND + local_id
// mirrors the connection hwnd pattern so callers can identify the engine at a glance.
GSY_ServerHwnd get_new_server_hwnd(GSY_EngineId engine_id)
{
    const auto local_id = server_id_generator.next();
    if (local_id == 0 || local_id >= MAX_SERVER_HWND)
    {
        LOG(ERROR) << "GSY server handle local_id is over limit";
        server_id_generator.remove(local_id);
        return InvalidServer;
    }
    return engine_id * MAX_SERVER_HWND + local_id;
}

void recycle_server_hwnd(const GSY_ServerHwnd hwnd)
{
    if (hwnd == InvalidServer)
        return;
    // extract local_id that was registered in the generator
    const auto local_id = hwnd % MAX_SERVER_HWND;
    server_id_generator.remove(local_id);
}

// ─── called from server.hpp after engine init ────────────────────────────────
void register_server_entry(GSY_ServerHwnd hwnd,
                            mqas::core::engine_base_interface* engine,
                            GSY_BaseServerContext* ctx)
{
    std::lock_guard<std::mutex> lock(server_mutex);
    server_map.insert({hwnd, ServerEntry{engine, ctx}});
}

// ─── GSY_stop_server ─────────────────────────────────────────────────────────
int GSY_stop_server(GSY_ServerHwnd hwnd)
{
    ServerEntry entry{};

    {
        std::lock_guard<std::mutex> lock(server_mutex);
        if (!server_map.contains(hwnd))
            return EC_InvalidHandler;
        entry = server_map[hwnd];
        server_map.erase(hwnd);
        recycle_server_hwnd(hwnd);
    }

    // All lsquic calls must happen on the main loop thread ✓
    // destroy_engine() is declared extern in engine.hpp and instantiated by
    // GSY_TEMPLATE_BY_ENGINE_TYPE_FOR_ENGINE; ~sub_engine() calls close() internally.
    push_task([entry, hwnd]()
    {
        destroy_engine(entry.engine);
        if (entry.ctx && entry.ctx->on_stopped)
            entry.ctx->on_stopped(EC_Ok, hwnd);
    });

    return EC_Ok;
}
