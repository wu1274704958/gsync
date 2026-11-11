#include "../lobby.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static std::atomic<bool> connected{false}, rejected{false};
static std::atomic<GSY_ConnectionHwnd> connection{0};
static void on_connect(int code, GSY_ConnectionHwnd handle) { if (code == EC_Ok) { connection = handle; connected = true; } else rejected = true; }
static void on_error(int, GSY_ConnectionHwnd) { rejected = true; }
static void on_disconnect(int, GSY_ConnectionHwnd) { rejected = true; }

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const bool unconfigured = std::strcmp(argv[2], "unconfigured") == 0;
    const bool expected = std::strcmp(argv[2], "accept") == 0 || unconfigured;
    GSY_Context global{};
    if (GSY_initialize(0, &global) != EC_Ok) return 3;
    GSY_BaseConnectionContext context{};
    context.on_connect = on_connect;
    context.on_error = on_error;
    context.on_disconnect = on_disconnect;
    auto handle = GSY_connect(1, argv[1], "127.0.0.1", 8101, &context);
    if (handle == InvalidConnection) { GSY_terminate(); return 4; }
    for (int i = 0; i < 700 && !connected && !rejected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool ok = expected ? connected.load() : rejected.load() && !connected.load();
    if (unconfigured && ok)
        ok = GSY_RequestConnectPeer(connection, 1, 2, 0) == EC_Fail;
    GSY_terminate();
    std::printf("lobby pin: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
