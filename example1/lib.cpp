#include "example1.h"
#include <memory>
#include <utility>
#include <mqas/io/context.h>

#include "mqas/context.h"
#include "mqas/core/def.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/unique_id_generator.h"
#include "mqas/tools/model/p2p_model.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"

constexpr GSY_ConnectionHwnd MAX_CONNECTION_HWND = 100000;

std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
std::unique_ptr<mqas::io::Context> io_cxt;
GSY_Context* global_context;

using HolePunchingStream = mqas::core::StreamVariant<
    mqas::core::StreamVariantPair<1, mqas::tools::p2p::P2PLobbyClientStream>,
    mqas::core::StreamVariantPair<2, mqas::tools::p2p::P2PHelperClientStream>>;

using HolePunchingEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<HolePunchingStream>>>;

using AllEngineType = std::tuple<HolePunchingEngine>;

std::unique_ptr<std::thread> main_thread;

std::unordered_map<GSY_EngineId,mqas::core::engine_base_interface *> engine_map;
std::unordered_map<GSY_ConnectionHwnd,std::weak_ptr<mqas::core::IConnect>> connect_map;
std::mutex engines_mutex;

std::atomic_bool is_running = false;
std::queue<std::function<void()>> task_queue;
std::atomic_bool task_queue_push = false;
std::atomic_bool task_queue_running = false;

mqas::tools::unique_id_generator<GSY_ConnectionHwnd,true> id_generator;

void mian_func();
void push_task(const std::function<void()> &task);
void destroy_engine(mqas::core::engine_base_interface* engine_base);
bool destroy_engine_by_conn_hwnd(const GSY_ConnectionHwnd hwnd);
bool destroy_engine_if_empty(mqas::core::engine_base_interface* engine_base);
std::optional<std::pair<GSY_EngineId,mqas::core::engine_base_interface*>> find_engine(const ::lsquic_engine* ec);
template<SIZE_T I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ConnectionHwnd connect_internal(GSY_EngineId engine_id,const char* config_file,
    const char* ip,short port,GSY_BaseConnectionContext* context);
GSY_ConnectionHwnd get_new_connection_hwnd(const GSY_EngineId engine_id);
void recycle_connection_hwnd(const GSY_ConnectionHwnd h);

int GSY_initialize(int flag,GSY_Context* cxt)
{
    if(is_running)
        return EC_AlreadyInitialized;
    is_running = true;
    global_context = cxt;
    main_thread = std::make_unique<std::thread>(mian_func);
    return EC_Ok;
}

int GSY_terminate()
{
    if (!is_running)
        return EC_NotInitialized;
    is_running = false;
    main_thread->join();
    // destroy all
    for (const auto &engine: engine_map | std::views::values) {
        destroy_engine(engine);
    }
    engine_map.clear();
    connect_map.clear();
    io_cxt.reset();
    context.reset();
    global_context = nullptr;
    return EC_Ok;
}

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


GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,
        const char* ip,short port,GSY_BaseConnectionContext* cxt) {
    return connect_internal<0,AllEngineType>(engine_id,config_file,ip,port,cxt);
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

void push_task(const std::function<void()> &task) {
    if (std::this_thread::get_id() == main_thread->get_id()) {
        task_queue.push(task);
    }else {
        while (task_queue_running.load(std::memory_order_acquire)) {}
        task_queue_push.store(true, std::memory_order_release);
        task_queue.push(task);
        task_queue_push.store(false, std::memory_order_release);
    }

}

void mian_func()
{
    context = std::make_unique<mqas::Context<mqas::core::InitFlags::BOTH>>();
    io_cxt = std::make_unique<mqas::io::Context>();

    const auto timer = io_cxt->make_handle<mqas::io::Timer>();
    timer->start([](mqas::io::Timer* t) {
        if (!is_running) {
            t->stop();
            io_cxt->stop();
        }
    },60,60);
    
    while (is_running) {
        if (task_queue_push.load(std::memory_order_acquire) == false){
            task_queue_running.store(true, std::memory_order_release);
            while(!task_queue.empty()) {
                task_queue.front()();
                task_queue.pop();
            }
            task_queue_running.store(false, std::memory_order_release);
        }
        io_cxt->run(mqas::io::Context::RunMode::ONCE);
    }
}

template<SIZE_T I, typename TU>
    requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU> >
void destroy_engine_internal(mqas::core::engine_base_interface* engine_base) {
    using EngineType = std::tuple_element_t<I, TU>;
    auto* engine = dynamic_cast<EngineType*>(engine_base);
    if (engine != nullptr) {
        delete engine;
    } else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            LOG(WARNING) << "Can not destroy engine, reason: not find type " << typeid(TU).name();
        } else {
            destroy_engine_internal<I + 1, TU>(engine_base);
        }
    }
}

template<SIZE_T I, typename TU>
    requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU> >
bool destroy_engine_if_empty_internal(mqas::core::engine_base_interface* engine_base) {
    using EngineType = std::tuple_element_t<I, TU>;
    auto* engine = dynamic_cast<EngineType*>(engine_base);
    if (engine != nullptr) {
        if (engine->get_real_engine()->connect_count() <= 0) {
            delete engine;
            return true;
        }
        return false;
    } else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            LOG(WARNING) << "Can not destroy engine, reason: not find type " << typeid(TU).name();
            return false;
        } else {
            return destroy_engine_internal<I + 1, TU>(engine_base);
        }
    }
}

template<SIZE_T I, typename TU>
requires std::is_base_of_v<mqas::core::engine_base_interface, std::tuple_element_t<I, TU>>
GSY_ConnectionHwnd connect_internal(GSY_EngineId engine_id,const char* config_file,const char* ip,short port,GSY_BaseConnectionContext* context) {
    if (engine_id - 1 == I) {
        using EngineType = std::tuple_element_t<I, TU>;
        const GSY_ConnectionHwnd hwnd = get_new_connection_hwnd(engine_id);
        if (hwnd == InvalidConnection) {
            if (global_context->on_error)
                global_context->on_error("Connection create handle failed",EC_ConnectOverLimit);
            return InvalidConnection;
        }
        std::lock_guard<std::mutex> _lock(engines_mutex);
        if (!engine_map.contains(engine_id)) {
            auto task = [
                config_file_path = std::string(config_file),
                ip_str = std::string(ip),
                port,
                context,hwnd]() {
                auto engine = new EngineType(*io_cxt);
                engine->init(config_file_path.c_str(),mqas::core::EngineFlags::None);
                engine->start_recv();
                engine->process_conns();

                sockaddr addr{};

                if(!mqas::io::Ip::str2addr_ipv4(ip_str.c_str(), port, addr)) {
                    destroy_connect(hwnd);
                    if (context->on_error)
                        context->on_error(EC_InvalidAddress,hwnd);
                    return;
                }

                auto connect = engine->get_engine()->connect(addr, N_LSQVER);
                engine->get_engine()->whitelist_addr.push_back(addr);
                engine->get_engine()->whitelist_port.push_back(mqas::io::Ip::addr_get_port(addr));
                auto conn = connect.lock();
                if (!conn) {
                    destroy_connect(hwnd);
                    if (context->on_error)
                        context->on_error(EC_ConnectFailed,hwnd);
                    return;
                }

                engine->get_engine()->on_connect_closed_signal.connect([engine,hwnd](std::shared_ptr<mqas::core::IConnect>) {
                    if (engine->get_engine()->connect_count() - 1 <= 0) {
                        push_task([hwnd]() { destroy_engine_by_conn_hwnd(hwnd); });
                    }
                });

                std::lock_guard<std::mutex> _lock(engines_mutex);
                engine_map.insert({hwnd / MAX_CONNECTION_HWND, engine});
                connect_map.insert({hwnd, connect});

                conn->set_cxt(context);

                conn->on_close_signal.connect([hwnd,context](mqas::core::IConnect& c) {
                    destroy_connect(hwnd);
                    if (context->on_disconnect) {
                        context->on_disconnect(EC_Disconnected,hwnd);
                    }
                });
                conn->on_hsk_done_signal.connect([hwnd,context](mqas::core::IConnect& c,::lsquic_hsk_status status) {
                    if (status == ::lsquic_hsk_status::LSQ_HSK_OK) {
                        if (context->on_connect) {
                            context->on_connect(EC_Ok,hwnd);
                        }
                    }
                });
            };
            push_task(task);
        }else {
            EngineType* engine = dynamic_cast<EngineType *>(engine_map[engine_id]);
            if (engine == nullptr) {
                recycle_connection_hwnd(hwnd);
                if (global_context->on_error)
                    global_context->on_error("The existing engine type do not match",EC_ConnectFailed);
                return InvalidConnection;
            }
            auto task = [engine,hwnd,context,ip_str = std::string(ip),port]() {

                sockaddr addr{};
                if(!mqas::io::Ip::str2addr_ipv4(ip_str.c_str(), port, addr)) {
                    destroy_connect(hwnd);
                    if (context->on_error)
                        context->on_error(EC_InvalidAddress,hwnd);
                    return;
                }

                auto connect = engine->get_engine()->connect(addr, N_LSQVER);
                engine->get_engine()->whitelist_addr.push_back(addr);
                engine->get_engine()->whitelist_port.push_back(mqas::io::Ip::addr_get_port(addr));
                auto conn = connect.lock();
                if (!conn) {
                    destroy_connect(hwnd);
                    if (context->on_error)
                        context->on_error(EC_ConnectFailed,hwnd);
                    return;
                }

                std::lock_guard<std::mutex> _lock(engines_mutex);

                connect_map.insert({hwnd, connect});
                conn->set_cxt(context);

                conn->on_close_signal.connect([hwnd,context](mqas::core::IConnect& c) {
                    destroy_connect(hwnd);
                    if (context->on_disconnect) {
                        context->on_disconnect(EC_Disconnected,hwnd);
                    }
                });
                conn->on_hsk_done_signal.connect([hwnd,context](mqas::core::IConnect& c,::lsquic_hsk_status status) {
                    if (status == ::lsquic_hsk_status::LSQ_HSK_OK) {
                        if (context->on_connect) {
                            context->on_connect(EC_Ok,hwnd);
                        }
                    }
                });
            };
            push_task(task);
        }
        return hwnd;
    }else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            return InvalidConnection;
        }else {
            return connect_internal<I + 1,TU>(engine_id,config_file,ip,port,context);
        }
    }
}

void destroy_engine(mqas::core::engine_base_interface* engine_base)
{
    if (engine_base == nullptr)
        return;
    destroy_engine_internal<0,AllEngineType>(engine_base);
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

bool destroy_engine_if_empty(mqas::core::engine_base_interface* engine_base)
{
    if (engine_base == nullptr)
        return false;
    return destroy_engine_if_empty_internal<0,AllEngineType>(engine_base);
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
