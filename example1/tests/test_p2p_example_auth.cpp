#include "../../mqas/examples/p2p_example_auth.h"
#include <array>
#include <chrono>

int main() {
    using mqas::tools::proto::p2p::NotifyConnectResult;
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const std::array<unsigned char, 32> key{1};
    const std::array<unsigned char, 16> identity{2};
    auto make_result = [&] {
        NotifyConnectResult result;
        result.set_ret(mqas::tools::proto::p2p::RetCode::ok);
        result.set_peer_id(2);
        result.set_psk_secret(key.data(), key.size());
        result.set_psk_identity(identity.data(), identity.size());
        result.set_psk_issued_at_ms(now - 1);
        result.set_psk_expires_at_ms(now + 60000);
        return result;
    };
    auto valid = make_result();
    if (!p2p_example::make_tls(valid, 1, CONFIG_PATH) || !valid.psk_secret().empty()) return 1;
    auto expired = make_result();
    expired.set_psk_expires_at_ms(now - 1);
    if (p2p_example::make_tls(expired, 1, CONFIG_PATH) || !expired.psk_secret().empty()) return 2;
    auto wrong_id = make_result();
    if (p2p_example::make_tls(wrong_id, 2, CONFIG_PATH) || !wrong_id.psk_secret().empty()) return 3;
    auto short_key = make_result();
    short_key.set_psk_secret("short");
    if (p2p_example::make_tls(short_key, 1, CONFIG_PATH) || !short_key.psk_secret().empty()) return 4;
    return 0;
}
