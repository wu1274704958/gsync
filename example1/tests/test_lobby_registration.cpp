#include "../lobby.h"
#include <mqas/context.h>
#include <mqas/io/context.h>
#include <mqas/io/timer.h>
#include <mqas/core/connect.h>
#include <mqas/core/stream.h>
#include <mqas/core/sub_engine.h>
#include <mqas/comm/locator.h>
#include <mqas/tools/model/p2p_model.h>
#include <mqas/tools/stream/p2p_lobby.h>
#include <mqas/tools/stream/p2p_helper.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

static std::atomic<GSY_ConnectionHwnd> connected{0};
static std::atomic<bool> registered{false};
static std::atomic<bool> io_rejected{false};
static GSY_LobbyStreamContext lobby_context{};

int main(int argc, char** argv) {
    const bool server = argc > 1 && std::string(argv[1]) == "server";
    if (server) {
        mqas::Context<mqas::core::InitFlags::BOTH> context;
        mqas::io::Context io;
        mqas::comm::locator::inst()->deposit<mqas::tools::p2p::p2p_model>();
        using Stream = mqas::core::StreamVariant<
            mqas::core::StreamVariantPair<1, mqas::tools::p2p::P2PLobbyStream>,
            mqas::core::StreamVariantPair<2, mqas::tools::p2p::P2PHelperStream>>;
        mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<Stream>>> engine(io);
        engine.init(SERVER_CONFIG_PATH, mqas::core::EngineFlags::Server);
        engine.start_recv();
        engine.process_conns();
        auto timer = io.make_shared<mqas::io::Timer>();
        const auto duration = argc > 2 ? std::stoul(argv[2]) : 5000ul;
        timer->start([&](mqas::io::Timer* t) { t->stop(); io.stop(); }, duration, 0);
        io.run();
        engine.close();
        return 0;
    }
    GSY_Context ctx{};
    if (GSY_initialize(0, &ctx) != EC_Ok) return 2;
    lobby_context.on_registration_success = [](ErrorCode code, GSY_StreamId, GSY_PeerId id) {
        registered = code == EC_Ok && id != INVALID_PID;
    };
    GSY_BaseConnectionContext connection{};
    connection.on_connect = [](int code, GSY_ConnectionHwnd hwnd) {
        if (code == EC_Ok) {
            io_rejected = GSY_RegisterToLobby(hwnd, "io", "", &lobby_context) == INVALID_SID;
            connected = hwnd;
        }
    };
    const auto hwnd = GSY_connect(1, CLIENT_CONFIG_PATH, "127.0.0.1", 8101, &connection);
    for (int i = 0; i < 300 && !connected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto sid = connected ? GSY_RegisterToLobby(hwnd, "test", "", &lobby_context) : INVALID_SID;
    for (int i = 0; i < 300 && !registered; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool ok = io_rejected && sid != INVALID_SID && registered;
    std::printf("lobby registration: %s\n", ok ? "PASS" : "FAIL");
    GSY_terminate();
    return ok ? 0 : 1;
}
