#if !defined(GSYNC_BUILD_UNIT_TEST)
#error This file is only for the unit-test build.
#endif
#include "../../src/common.h"
#include "mqas/io/ip.h"
#include <cstdint>

extern "C" GSYNC_EXTERN uintptr_t GSY_test_server_engine(GSY_ServerHwnd hwnd)
{
    return push_task_with_result<uintptr_t>([hwnd] {
        std::shared_ptr<ServerEntry> server;
        {
            std::lock_guard lock(server_mutex);
            const auto it = server_map.find(hwnd);
            if (it == server_map.end()) return uintptr_t{0};
            server = it->second;
        }
        const auto owner = server->owner.lock();
        return owner ? reinterpret_cast<uintptr_t>(owner->engine) : uintptr_t{0};
    });
}
extern "C" GSYNC_EXTERN uint16_t GSY_test_server_port(GSY_ServerHwnd hwnd)
{
    return push_task_with_result<uint16_t>([hwnd] {
        std::shared_ptr<ServerEntry> server;
        {
            std::lock_guard lock(server_mutex);
            const auto it = server_map.find(hwnd);
            if (it == server_map.end()) return uint16_t{0};
            server = it->second;
        }
        const auto owner = server->owner.lock();
        return owner ? mqas::io::Ip::addr_get_port(reinterpret_cast<const sockaddr&>(owner->bind_addr)) : uint16_t{0};
    });
}
extern "C" GSYNC_EXTERN size_t GSY_test_server_engine_count()
{
    return push_task_with_result<size_t>([] { return server_engines.size(); });
}
