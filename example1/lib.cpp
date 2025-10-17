#include "example1.h"
#include <memory>
#include <mqas/io/context.h>

#include "mqas/context.h"
#include "mqas/core/def.h"
#include "mqas/core/engine_driver.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/model/p2p_model.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"

std::unique_ptr<mqas::Context<mqas::core::InitFlags::BOTH>> context;
std::unique_ptr<mqas::io::Context> io_cxt;
SICallback global_callback;

struct EngineWrapper {
    std::shared_ptr<mqas::core::engine_base_interface> engine_base;
    ICallback event_callback;
};

using HolePunchingStream = mqas::core::StreamVariant<
    mqas::core::StreamVariantPair<1, mqas::tools::p2p::P2PLobbyClientStream>,
    mqas::core::StreamVariantPair<2, mqas::tools::p2p::P2PHelperClientStream>>;

using HolePunchingEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<HolePunchingStream>>>;

using AllEngineType = std::tuple<HolePunchingEngine>;

std::unique_ptr<std::thread> main_thread;

std::unordered_map<uint32_t, EngineWrapper> engines;

std::atomic_bool is_running = false;
std::queue<std::function<void()>> task_queue;
std::atomic_bool task_queue_push = false;
std::atomic_bool task_queue_running = false;

void mian_func();
void push_task(const std::function<void()> &task);

int GSY_initialize(int flag, SICallback error_callback)
{
    if(is_running)
        return EC_AlreadyInitialized;
    is_running = true;
    context = std::make_unique<mqas::Context<mqas::core::InitFlags::BOTH>>();
    io_cxt = std::make_unique<mqas::io::Context>();
    global_callback = error_callback;
    main_thread = std::make_unique<std::thread>(mian_func);
    return EC_Ok;
}

void destroy_engine(std::shared_ptr<mqas::core::engine_base_interface>& engine_base);

int GSY_terminate()
{
    if (!is_running)
        return EC_NotInitialized;
    is_running = false;
    main_thread->join();
    // destroy all
    for (auto &[engine_base, event_callback]: engines | std::views::values) {
        destroy_engine(engine_base);
    }
    engines.clear();
    io_cxt.reset();
    context.reset();
    global_callback = nullptr;
    return EC_Ok;
}

int GSY_connect_hole_punching_server(const char* config_file,const char* name,const char* psd,ICallback callback,PDCallback req_connect_cb)
{
    const uint32_t id = engines.size() + 1;
    if (engines.contains(id))
        return EC_EngineCountLimitExceeded;

    auto task = [
        config_file_path = std::string(config_file),
        name_str = std::string(name),
        psd_str = std::string(psd),
        callback,id,req_connect_cb]() {
        auto engine =  std::make_shared<HolePunchingEngine>(*io_cxt);
        engine->init(config_file_path.c_str(),mqas::core::EngineFlags::None);
        engine->start_recv();
        engine->process_conns();

        sockaddr addr{};
        const auto ip = toml::find<std::string>(*engine->get_engine()->get_config(),"client", "ip");
        const auto port = toml::find<int>(*engine->get_engine()->get_config(), "client", "port");

        if(!mqas::io::Ip::str2addr_ipv4(ip.c_str(), port, addr)) {
            if (callback != nullptr)
                callback(EC_InvalidAddress);
            return;
        }

        auto connect = engine->get_engine()->connect(addr, N_LSQVER);
        engine->get_engine()->whitelist_addr.push_back(addr);
        engine->get_engine()->whitelist_port.push_back(mqas::io::Ip::addr_get_port(addr));

        auto conn = connect.lock();
        conn->make_stream([name_str = std::move(name_str),req_connect_cb](std::weak_ptr<HolePunchingStream> stream) {
            auto s = stream.lock();
            mqas::tools::proto::p2p::ReqRegistePeer msg;
            if (!name_str.empty())
                msg.set_name(name_str);
            s->req_change<mqas::tools::p2p::P2PLobbyClientStream, mqas::tools::p2p::ReqRegistePeerPair>(msg);
            auto lobby_stream = s->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();

            lobby_stream->on_want_connect.connect([req_connect_cb](const mqas::tools::proto::p2p::PeerData& peer) {
                if (req_connect_cb != nullptr) {
                    PeerData pd{.id = peer.id(),.name = peer.name().c_str()};
                    req_connect_cb(&pd);
                }
            });

            lobby_stream->on_get_respond.connect([stream](std::shared_ptr<mqas::tools::proto::p2p::RespondConnectPeer> respond)
            {

            });

            lobby_stream->on_change_helper = [stream](const mqas::tools::proto::p2p::ReqRespondPeerReqConnect& msg)
            {
                auto s = stream.lock();
                s->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqRespondPeerReqConnectPair>(msg);
            };
            lobby_stream->on_change_helper_by_req = [stream](const mqas::tools::proto::p2p::ReqConnectPeer& msg)
            {
                auto s = stream.lock();
                s->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqConnectPeerPair>(msg);
            };

        });
        engines.insert({id,EngineWrapper{.engine_base = std::move(engine),.event_callback = callback}});
    };

    push_task(task);

    return 0;
}

int GSY_disconnect_hole_punching_server(int handler)
{
    return EC_Ok;
}

int GSY_is_connected_hole_punching_server(int handler)
{
    if (engines.contains(handler)) {
        const auto engine = std::dynamic_pointer_cast<HolePunchingEngine>(engines[handler].engine_base);
        return engine != nullptr && mqas::core::engine_base_interface::is_valid(engine.get()) && engine->get_engine()->connect_count() == 1 ? 1 : 0;
    }
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
void destroy_engine_internal(std::shared_ptr<mqas::core::engine_base_interface> &engine_base) {
    using EngineType = std::tuple_element_t<I, TU>;
    auto engine = std::dynamic_pointer_cast<EngineType>(engine_base);
    if (engine != nullptr) {
        engine.reset();
    } else {
        if constexpr (I + 1 >= std::tuple_size_v<TU>) {
            LOG(WARNING) << "Can not destroy engine, reason: not find type " << typeid(TU).name();
        } else {
            destroy_engine_internal<I + 1, TU>(engine_base);
        }
    }
}

void destroy_engine(std::shared_ptr<mqas::core::engine_base_interface>& engine_base)
{
    destroy_engine_internal<0,AllEngineType>(engine_base);
}

