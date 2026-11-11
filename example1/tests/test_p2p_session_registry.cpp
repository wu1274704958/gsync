#include "../p2p_session_registry.h"
#include "../p2p_session.h"
#include <mqas/io/udp.h>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#include <cstdio>
void p2p_session_remove_stream(GSY_ConnectionHwnd, GSY_StreamId);
void p2p_session_remove_connection(GSY_ConnectionHwnd);
void p2p_session_shutdown();
int main() {
    auto socket = std::make_shared<mqas::io::UdpSocket>();
    auto session = std::make_shared<P2PSession>();
    session->self_id = 7; session->peer_id = 8;
    const auto h = p2p_session_register(1, 5, socket, session);
    std::shared_ptr<mqas::io::UdpSocket> locked;
    std::shared_ptr<const P2PSession> acquired;
    bool ok = h && p2p_session_acquire(h, locked, acquired) == EC_Ok && locked == socket && acquired == session;
    std::shared_ptr<mqas::io::UdpSocket> other_socket;
    std::shared_ptr<const P2PSession> other_session;
    ok &= p2p_session_acquire(h, other_socket, other_session) == EC_SessionBusy;
    ok &= p2p_session_remove(h) == EC_SessionBusy;
    p2p_session_release_attempt(h, acquired);
    ok &= p2p_session_acquire(h, other_socket, other_session) == EC_Ok;
    ok &= p2p_session_consume(h, other_session);
    ok &= p2p_session_acquire(h, other_socket, other_session) == EC_InvalidHandler;
    ok &= p2p_session_remove(h) == EC_InvalidHandler;
    const auto released = p2p_session_register(1, 6, socket, session);
    ok &= p2p_session_remove(released) == EC_Ok && p2p_session_remove(released) == EC_InvalidHandler;
    auto another = std::make_shared<P2PSession>();
    const auto first = p2p_session_register(1, 10, socket, session);
    const auto second = p2p_session_register(1, 10, socket, another);
    ok &= first != second && p2p_session_acquire(first, locked, acquired) == EC_Ok &&
        p2p_session_acquire(second, other_socket, other_session) == EC_Ok &&
        locked == other_socket && acquired != other_session;
    ok &= p2p_session_consume(first, acquired) && p2p_session_consume(second, other_session);
    const auto expired = p2p_session_register(1, 7, socket, session);
    p2p_session_remove_stream(1, 7);
    ok &= p2p_session_acquire(expired, other_socket, other_session) == EC_InvalidHandler;
    const auto gone = p2p_session_register(1, 8, socket, session);
    p2p_session_remove_connection(1);
    ok &= p2p_session_acquire(gone, other_socket, other_session) == EC_InvalidHandler;
    std::atomic_bool valid{true};
    std::vector<std::thread> threads;
    const auto contested = p2p_session_register(1, 11, socket, session);
    std::barrier start(8);
    std::atomic_int winners{0};
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        std::shared_ptr<mqas::io::UdpSocket> s;
        std::shared_ptr<const P2PSession> c;
        start.arrive_and_wait();
        const auto result = p2p_session_acquire(contested, s, c);
        if (result == EC_Ok) ++winners;
        else if (result != EC_SessionBusy) valid = false;
    });
    for (auto& thread : threads) thread.join();
    ok &= winners == 1 && valid.load();
    p2p_session_release_attempt(contested, session);
    ok &= p2p_session_remove(contested) == EC_Ok;
    threads.clear();
    for (int i = 0; i < 8; ++i) threads.emplace_back([&, i] {
        for (int j = 0; j < 500; ++j) {
            auto expected = std::make_shared<P2PSession>(); expected->peer_id = i + 1;
            const auto handle = p2p_session_register(10 + i, 9, socket, expected);
            std::shared_ptr<mqas::io::UdpSocket> s;
            std::shared_ptr<const P2PSession> c;
            if (p2p_session_acquire(handle, s, c) != EC_Ok || s != socket || c != expected) valid = false;
            p2p_session_release_attempt(handle, c);
            if (p2p_session_remove(handle) != EC_Ok) valid = false;
        }
    });
    for (auto& thread : threads) thread.join();
    ok &= valid.load();
    const auto stale = p2p_session_register(1, 9, socket, session);
    p2p_session_shutdown();
    ok &= p2p_session_remove(stale) == EC_InvalidHandler;
    std::printf("P2P session handles: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
