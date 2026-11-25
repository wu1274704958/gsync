#include "core/server.h"
#include "mqas/tools/unique_id_generator.h"
#include "mqas/core/connect.h"
#include "mqas/core/engine_settings.h"
#include "../common.h"
#include <algorithm>
#include <condition_variable>

std::map<EngineKey, std::shared_ptr<ServerEngineEntry>> server_engines;
std::unordered_map<GSY_ServerHwnd, std::shared_ptr<ServerEntry>> server_map;
std::mutex server_mutex;
bool server_shutting_down = false; // guarded by server_mutex
static mqas::tools::unique_id_generator<GSY_ServerHwnd, true> server_id_generator;
static std::condition_variable server_shutdown_cv;
static bool server_shutdown_pending = false; // I/O thread only
static bool server_shutdown_complete = false; // guarded by server_mutex

static void check_server_shutdown()
{
    if (!server_shutdown_pending || !server_engines.empty()) return;
    std::lock_guard lock(server_mutex);
    if (!server_map.empty()) return;
    server_shutdown_pending = false;
    server_shutdown_complete = true;
    server_shutdown_cv.notify_all();
}

GSY_ServerHwnd get_new_server_hwnd(GSY_EngineId engine_id)
{
    const auto id = server_id_generator.next();
    if (id == 0 || id >= MAX_SERVER_HWND) {
        server_id_generator.remove(id);
        return InvalidServer;
    }
    return engine_id * MAX_SERVER_HWND + id;
}
void recycle_server_hwnd(GSY_ServerHwnd hwnd)
{
    if (hwnd != InvalidServer) server_id_generator.remove(hwnd % MAX_SERVER_HWND);
}
bool server_endpoint_overlaps(const sockaddr& addr)
{
    for (const auto& [key, entry] : server_engines)
        if (mqas::core::bind_endpoints_overlap(reinterpret_cast<const sockaddr&>(entry->bind_addr), addr)) return true;
    return false;
}
void cleanup_server_engine(const std::shared_ptr<ServerEngineEntry>& entry)
{
    if (!entry || !entry->servers.empty() || !entry->engine) return;
    auto it = server_engines.find(entry->key);
    if (it == server_engines.end() || it->second != entry) return;
    if (destroy_engine_if_empty(entry->engine)) {
        entry->engine = nullptr;
        server_engines.erase(it);
        check_server_shutdown();
    }
}
void finish_server_stop(const std::shared_ptr<ServerEntry>& server)
{
    if (!server->stopping || server->finalized || !server->connections.empty()) return;
    if (server->cleanup) server->cleanup(server);
    server->finalized = true;
    auto owner = server->owner.lock();
    if (owner) {
        std::erase(owner->servers, server);
        push_task([owner] { cleanup_server_engine(owner); }, true);
    }
    {
        std::lock_guard lock(server_mutex);
        auto it = server_map.find(server->hwnd);
        if (it != server_map.end() && it->second == server) server_map.erase(it);
    }
    if (server->ctx && server->ctx->on_stopped) server->ctx->on_stopped(EC_Ok, server->hwnd);
    recycle_server_hwnd(server->hwnd);
    check_server_shutdown();
}
void fail_server_start(const std::shared_ptr<ServerEntry>& server, ErrorCode error)
{
    bool cancelled = false;
    {
        std::lock_guard lock(server_mutex);
        cancelled = server->stopping;
        if (!cancelled) {
            server->finalized = true;
            auto it = server_map.find(server->hwnd);
            if (it != server_map.end() && it->second == server) server_map.erase(it);
        }
    }
    if (cancelled) { finish_server_stop(server); return; }
    if (server->ctx && server->ctx->on_error) server->ctx->on_error(error, server->hwnd);
    recycle_server_hwnd(server->hwnd);
}
int GSY_stop_server(GSY_ServerHwnd hwnd)
{
    std::shared_ptr<ServerEntry> server;
    {
        std::lock_guard lock(server_mutex);
        auto it = server_map.find(hwnd);
        if (it == server_map.end() || it->second->stopping.exchange(true)) return EC_InvalidHandler;
        server = it->second;
    }
    push_task([server] {
        std::vector<std::shared_ptr<mqas::core::IConnect>> connections;
        for (const auto& [id, weak] : server->connections) if (auto conn = weak.lock()) connections.push_back(conn);
        for (const auto& conn : connections) if (!conn->is_closed()) conn->close();
        finish_server_stop(server);
    }, true);
    return EC_Ok;
}
void shutdown_server_engines()
{
    std::vector<std::shared_ptr<ServerEngineEntry>> engines;
    std::vector<std::shared_ptr<ServerEntry>> servers;
    {
        std::lock_guard lock(server_mutex);
        server_shutting_down = true;
        server_shutdown_complete = false;
        for (auto& [id, server] : server_map) { server->stopping = true; servers.push_back(server); }
    }
    server_shutdown_pending = true;
    for (auto& [key, entry] : server_engines) engines.push_back(entry);
    for (auto& server : servers) {
        std::vector<std::shared_ptr<mqas::core::IConnect>> connections;
        for (const auto& [id, weak] : server->connections) if (auto conn = weak.lock()) connections.push_back(conn);
        for (const auto& conn : connections) if (!conn->is_closed()) conn->close();
        finish_server_stop(server);
    }
    for (auto& entry : engines) cleanup_server_engine(entry);
    check_server_shutdown();
}
void wait_for_server_shutdown()
{
    std::unique_lock lock(server_mutex);
    server_shutdown_cv.wait(lock, [] { return server_shutdown_complete; });
}
