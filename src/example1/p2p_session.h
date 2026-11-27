#pragma once
#include <array>
#include <cstdint>
#include <mqas/io/ip.h>
#include <chrono>

// One helper negotiation. Its handle is consumed after successful authentication.
struct P2PSession {
    P2PSession() = default;
    P2PSession(const P2PSession&) = delete;
    P2PSession& operator=(const P2PSession&) = delete;
    P2PSession(P2PSession&&) = delete;
    P2PSession& operator=(P2PSession&&) = delete;
    std::array<unsigned char, 32> secret{};
    std::array<unsigned char, 16> identity{};
    std::array<unsigned char, 16> negotiation_id{};
    uint32_t peer_id = 0;
    uint32_t self_id = 0;
    bool is_server = false;
    mqas::io::SocketAddress peer_address;
    uint64_t issued_at_ms = 0;
    uint64_t expires_at_ms = 0;
    bool valid_now() const {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now >= 0 && static_cast<uint64_t>(now) >= issued_at_ms &&
            static_cast<uint64_t>(now) < expires_at_ms;
    }
    ~P2PSession();
};
