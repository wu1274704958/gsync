#include "gsync/core/connection.h"
#include <memory>
#include <utility>
#include <condition_variable>
#include <mqas/io/context.h>
#include <mqas/io/udp.h>

#include "mqas/context.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/stream.h"
#include "mqas/tools/unique_id_generator.h"
#include "mqas/tools/model/p2p_model.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "common.h"

mqas::tools::unique_id_generator<GSY_ConnectionHwnd,true> id_generator;

std::unordered_map<GSY_ConnectionHwnd,std::weak_ptr<mqas::core::IConnect>> connect_map;
std::mutex connect_mutex;

// Accessed only on the SDK I/O thread. Socket lifetime is owned by its users.
static std::unordered_map<GSY_SocketHandle, std::weak_ptr<mqas::io::UdpSocket>> socket_handles;
static GSY_SocketHandle next_socket_handle = 1;

void gsy_socket_handles_shutdown() { socket_handles.clear(); }
std::shared_ptr<mqas::io::UdpSocket> gsy_get_socket(GSY_SocketHandle handle) {
    std::erase_if(socket_handles, [](const auto& entry) { return entry.second.expired(); });
    const auto it = socket_handles.find(handle);
    return it == socket_handles.end() ? nullptr : it->second.lock();
}

ErrorCode GSY_GetConnectionSocket(GSY_ConnectionHwnd handle, GSY_SocketHandle* out)
{
    if (!out) return EC_Fail;
    *out = GSY_INVALID_SOCKET;
    if (!is_running) return EC_NotInitialized;
    return push_task_with_result<ErrorCode>([handle, out] {
        std::shared_ptr<mqas::core::IConnect> conn;
        {
            std::lock_guard lock(connect_mutex);
            auto it = connect_map.find(handle);
            if (it != connect_map.end()) conn = it->second.lock();
        }
        if (!conn || conn->is_closed()) return EC_InvalidHandler;
        const auto cxt = conn->get_engine_cxt();
        const auto engine = cxt ? cxt->engine.lock() : nullptr;
        const auto socket = engine ? engine->get_socket() : nullptr;
        if (!socket) return EC_InvalidHandler;
        if (!next_socket_handle) return EC_ConnectOverLimit;
        const auto id = next_socket_handle++;
        socket_handles.emplace(id, socket);
        *out = id;
        return EC_Ok;
    });
}

ErrorCode GSY_ReleaseSocket(GSY_SocketHandle handle)
{
    if (!is_running) return EC_NotInitialized;
    return push_task_with_result<ErrorCode>([handle] {
        return socket_handles.erase(handle) ? EC_Ok : EC_InvalidHandler;
    });
}

ErrorCode GSY_AreSameSocket(GSY_SocketHandle first, GSY_SocketHandle second, int* same)
{
    if (!same) return EC_Fail;
    *same = 0;
    if (!is_running) return EC_NotInitialized;
    return push_task_with_result<ErrorCode>([first, second, same] {
        const auto a = gsy_get_socket(first);
        const auto b = gsy_get_socket(second);
        if (!a || !b) return EC_InvalidHandler;
        *same = a == b ? 1 : 0;
        return EC_Ok;
    });
}

void destroy_connect(const GSY_ConnectionHwnd hwnd, bool recycle) {
    p2p_session_remove_connection(hwnd);
    std::shared_ptr<mqas::core::IConnect> conn;
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        auto it = connect_map.find(hwnd);
        if (it == connect_map.end()) return;
        conn = it->second.lock();
        connect_map.erase(it);
    }
    if (auto owner = connection_engines.find(hwnd); owner != connection_engines.end()) {
        auto entry = owner->second;
        connection_engines.erase(owner);
        push_task([entry] { cleanup_connection_engine(entry); }, true);
    }
    if (recycle) recycle_connection_hwnd(hwnd);
    if (conn && !conn->is_closed()) conn->close();
}

void push_destroy_connect_task(GSY_ConnectionHwnd hwnd) {
    push_task([hwnd]() {
        std::shared_ptr<mqas::core::IConnect> conn;
        {
            std::lock_guard<std::mutex> lock(connect_mutex);
            auto it = connect_map.find(hwnd);
            if (it != connect_map.end()) conn = it->second.lock();
        }
        // Keep the handle registered until its close callback runs so that a
        // recycled handle cannot be mistaken for this connection.
        if (conn) {
            if (!conn->is_closed()) conn->close();
        } else destroy_connect(hwnd);
    }, true);
}

namespace {
std::mutex outbound_mutex;
std::condition_variable outbound_empty;
std::unordered_map<GSY_ConnectionHwnd, std::function<void()>> outbound_connections;
bool accepting_outbound = false;
}

void initialize_outbound_connections() {
    std::lock_guard lock(outbound_mutex);
    accepting_outbound = true;
}

bool register_outbound_connection(GSY_ConnectionHwnd handle, std::function<void()> cancel) {
    std::lock_guard lock(outbound_mutex);
    return accepting_outbound && outbound_connections.emplace(handle, std::move(cancel)).second;
}

void unregister_outbound_connection(GSY_ConnectionHwnd handle) {
    {
        std::lock_guard lock(outbound_mutex);
        outbound_connections.erase(handle);
        recycle_connection_hwnd(handle);
    }
    outbound_empty.notify_all();
}

void shutdown_outbound_connections() {
    std::vector<std::function<void()>> cancels;
    {
        std::lock_guard lock(outbound_mutex);
        accepting_outbound = false;
        for (const auto& [handle, cancel] : outbound_connections) cancels.push_back(cancel);
    }
    for (const auto& cancel : cancels) push_task(cancel, true);
    std::unique_lock lock(outbound_mutex);
    outbound_empty.wait(lock, [] { return outbound_connections.empty(); });
}

int GSY_disconnect(GSY_ConnectionHwnd handler)
{
    std::function<void()> cancel;
    {
        std::lock_guard lock(outbound_mutex);
        const auto it = outbound_connections.find(handler);
        if (it != outbound_connections.end()) cancel = it->second;
    }
    if (cancel) {
        push_task(cancel, true);
        return EC_Ok;
    }
    {
        std::lock_guard lock(connect_mutex);
        if (connect_map.contains(handler)) {
            push_destroy_connect_task(handler);
            return EC_Ok;
        }
    }
    return EC_InvalidHandler;
}


int GSY_is_connected(unsigned int handler)
{
    if (std::this_thread::get_id() == main_thread->get_id()) {
        assert(false);//"Unexcepted!!!"
    }
    connect_mutex.lock();
    if (connect_map.contains(handler)) {
        std::atomic_bool completed = false;
        std::atomic_bool result = false;
        push_task([&completed,&result,handler]() {
            {
                std::lock_guard<std::mutex> lock(connect_mutex);
                const auto it = connect_map.find(handler);
                if (it == connect_map.end()) {
                    result.store(false, std::memory_order::release);
                } else if (const auto conn = it->second.lock()) {
                    result.store(!conn->is_closed() && conn->security_info().handshake == mqas::core::HandshakeState::Succeeded,
                        std::memory_order::release);
                } else {
                    result.store(false, std::memory_order::release);
                }
            }
            completed.store(true, std::memory_order::release);
        });
        connect_mutex.unlock();
        while (!completed.load(std::memory_order::acquire)) {}
        return result.load(std::memory_order::acquire) ? 1 : 0;
    }
    connect_mutex.unlock();
    return 0;
}

void cleanup_connection_engine(std::weak_ptr<EngineEntry> weak) {
    auto entry = weak.lock();
    if (!entry) return;
    auto it = engine_map.find(entry->key);
    if (it == engine_map.end() || it->second != entry) return;
    if (destroy_engine_if_empty(entry->engine)) {
        entry->engine = nullptr;
        engine_map.erase(it);
    }
}

GSY_ConnectionHwnd get_new_connection_hwnd(const GSY_EngineId engine_id) {
    const auto id = id_generator.next();
    if (id >= MAX_CONNECTION_HWND) {
        LOG(ERROR) << "GSY connection handle is over limit";
        id_generator.remove(id);
        return InvalidConnection;
    }
    return engine_id * MAX_CONNECTION_HWND + id;
}
void recycle_connection_hwnd(const GSY_ConnectionHwnd h) {
    if (h <= MAX_CONNECTION_HWND)
        return;
    const auto id = h % MAX_CONNECTION_HWND;
    id_generator.remove(id);
}

