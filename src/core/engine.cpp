#include "core/engine.h"
#include <memory>
#include <utility>
#include <mqas/io/context.h>

#include "mqas/context.h"
#include "mqas/core/def.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/stream.h"
#include "mqas/tools/model/p2p_model.h"
#include "../common.h"


std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
std::unique_ptr<mqas::io::Context> io_cxt;
GSY_Context* global_context;

std::unique_ptr<std::thread> main_thread;

std::unordered_map<GSY_EngineId,mqas::core::engine_base_interface *> engine_map;
std::mutex engines_mutex;

std::atomic_bool is_running = false;
std::queue<std::function<void()>> task_queue;
std::atomic_bool task_queue_push = false;
std::atomic_bool task_queue_running = false;

void mian_func();

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


