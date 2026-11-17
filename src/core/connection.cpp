#include "core/connection.h"
#include <memory>
#include <utility>
#include <mqas/io/context.h>

#include "mqas/context.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/stream.h"
#include "mqas/tools/unique_id_generator.h"
#include "mqas/tools/model/p2p_model.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../common.h"

mqas::tools::unique_id_generator<GSY_ConnectionHwnd,true> id_generator;

std::unordered_map<GSY_ConnectionHwnd,std::weak_ptr<mqas::core::IConnect>> connect_map;
std::mutex connect_mutex;

void destroy_connect(const GSY_ConnectionHwnd hwnd) {
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
    recycle_connection_hwnd(hwnd);
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

int GSY_disconnect(GSY_ConnectionHwnd handler)
{
    std::lock_guard<std::mutex> lock(connect_mutex);
    if (!connect_map.contains(handler))
        return EC_InvalidHandler;
    push_destroy_connect_task(handler);
    return EC_Ok;
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
            bool expired = false;
            {
                std::lock_guard<std::mutex> lock(connect_mutex);
                const auto it = connect_map.find(handler);
                if (it == connect_map.end()) {
                    result.store(false, std::memory_order::release);
                } else if (const auto conn = it->second.lock()) {
                    result.store(conn->get_hsk_status() == ::lsquic_hsk_status::LSQ_HSK_OK, std::memory_order::release);
                } else {
                    result.store(false, std::memory_order::release);
                    expired = true;
                }
            }
            if (expired) destroy_connect(handler);
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

