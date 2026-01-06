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

bool destroy_engine_by_conn_hwnd(const GSY_ConnectionHwnd hwnd);
bool destroy_engine_if_empty(mqas::core::engine_base_interface* engine_base);
std::optional<std::pair<GSY_EngineId,mqas::core::engine_base_interface*>> find_engine(const ::lsquic_engine* ec);
GSY_ConnectionHwnd get_new_connection_hwnd(const GSY_EngineId engine_id);
void recycle_connection_hwnd(const GSY_ConnectionHwnd h);


void destroy_connect(const GSY_ConnectionHwnd hwnd) {
    std::lock_guard<std::mutex> lock(engines_mutex);
    if (connect_map.contains(hwnd)) {
        //close connection
        const auto connection = connect_map[hwnd];
        connect_map.erase(hwnd);
        if (!connection.expired()) {
            if (const auto conn = connection.lock();conn) {
                if (!conn->is_closed()) {
                    conn->close();
                }
            }
        }
    }
    recycle_connection_hwnd(hwnd);
}

void push_destroy_connect_task(GSY_ConnectionHwnd hwnd) {
    auto task = [hwnd]() {
        destroy_connect(hwnd);
    };
    push_task(task);
}

int GSY_disconnect(GSY_ConnectionHwnd handler)
{
    std::lock_guard<std::mutex> lock(engines_mutex);
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
    engines_mutex.lock();
    if (connect_map.contains(handler)) {
        std::atomic_bool completed = false;
        std::atomic_bool result = false;
        push_task([&completed,&result,handler]() {
            std::lock_guard<std::mutex> lock(engines_mutex);
            if (!connect_map.contains(handler)) {
                result.store(false,std::memory_order::release);
            }else {
                const auto conn = connect_map.at(handler).lock();
                if (!conn) {
                    result.store(false,std::memory_order::release);
                    destroy_connect(handler);
                }else {
                    result.store(conn->get_hsk_status() == ::lsquic_hsk_status::LSQ_HSK_OK,std::memory_order::release);
                }
            }
            completed.store(true,std::memory_order::release);
        });
        engines_mutex.unlock();
        while (!completed.load(std::memory_order::acquire)) {}
        return result.load(std::memory_order::acquire) ? 1 : 0;
    }
    engines_mutex.unlock();
    return 0;
}

bool destroy_engine_by_conn_hwnd(const GSY_ConnectionHwnd hwnd) {
    if (const auto engine_id = hwnd / MAX_CONNECTION_HWND; engine_map.contains(engine_id)) {
        if (destroy_engine_if_empty(engine_map.at(engine_id))) {
            engine_map.erase(engine_id);
            return true;
        }
    }
    return false;
}

std::optional<std::pair<GSY_EngineId,mqas::core::engine_base_interface*>> find_engine(const ::lsquic_engine* ec)
{
    for (auto &pair: engine_map) {
        if (pair.second->get_origin() == ec) {
            return {pair};
        }
    }
    return {};
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
