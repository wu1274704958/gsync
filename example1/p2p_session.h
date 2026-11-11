#pragma once
#include <array>
#include <cstdint>
#include <string>

// One helper negotiation. Its handle is consumed after successful authentication.
struct P2PSession {
    P2PSession() = default;
    P2PSession(const P2PSession&) = delete;
    P2PSession& operator=(const P2PSession&) = delete;
    P2PSession(P2PSession&&) = delete;
    P2PSession& operator=(P2PSession&&) = delete;
    std::array<unsigned char, 32> secret{};
    std::array<unsigned char, 16> identity{};
    uint32_t peer_id = 0;
    uint32_t self_id = 0;
    bool is_server = false;
    std::string peer_ip;
    uint16_t peer_port = 0;
    uint64_t issued_at_ms = 0;
    uint64_t expires_at_ms = 0;
    ~P2PSession();
};
