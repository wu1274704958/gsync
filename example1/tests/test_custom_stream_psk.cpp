#include <mqas/context.h>
#include <mqas/io/context.h>
#include <mqas/io/timer.h>
#include <mqas/io/ip.h>
#include <mqas/core/connect.h>
#include <mqas/core/stream.h>
#include <mqas/core/sub_engine.h>
#include <cstdio>
#include <string>
#include <array>
#include <cstdlib>
#include <openssl/ssl.h>
#include <mqas/tools/model/psk_tls_context.h>
#include "../../mqas/examples/p2p_example_auth.h"

static std::shared_ptr<const mqas::core::TlsConfiguration> make_psk_context(bool server, bool mismatch) {
    std::array<unsigned char, 32> key{1, 2, 3};
    if (mismatch && !server) key[0] ^= 0xff;
    const std::array<unsigned char, 16> identity{4, 5, 6};
    mqas::tools::proto::p2p::NotifyConnectResult result;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.set_ret(mqas::tools::proto::p2p::RetCode::ok);
    result.set_is_server(server);
    result.set_peer_id(server ? 1 : 2);
    result.set_psk_secret(key.data(), key.size());
    result.set_psk_identity(identity.data(), identity.size());
    result.set_psk_issued_at_ms(static_cast<uint64_t>(now));
    result.set_psk_expires_at_ms(static_cast<uint64_t>(now) + 120000);
    const char* client_config = std::getenv("MQAS_TEST_CLIENT_CONFIG");
    auto tls = p2p_example::make_tls(result, server ? 2 : 1,
        server ? SERVER_CONFIG_PATH : (client_config ? client_config : CLIENT_CONFIG_PATH));
    return result.psk_secret().empty() ? tls : nullptr;
}

static bool changed = false;
static bool acknowledged = false;
class CustomStream : public mqas::core::IStreamVariant {
public:
    mqas::core::StreamVariantErrcode on_local_change(const std::span<uint8_t>&,
                                                       std::vector<uint8_t>&) {
        return mqas::core::StreamVariantErrcode::ok;
    }
    mqas::core::StreamVariantErrcode on_change(const std::span<uint8_t>&,
                                                std::vector<uint8_t>&) {
        changed = true;
        return mqas::core::StreamVariantErrcode::ok;
    }
    void on_peer_change_ack(mqas::core::StreamVariantErrcode code, const std::span<uint8_t>&) {
        acknowledged = code == mqas::core::StreamVariantErrcode::ok;
    }
};
using Variant = mqas::core::StreamVariant<mqas::core::StreamVariantPair<1, CustomStream>>;
using Connection = mqas::core::Connect<Variant>;
using Engine = mqas::core::sub_engine<mqas::core::engine<Connection>>;

int main(int argc, char** argv) {
    const bool server = argc > 1 && std::string(argv[1]) == "server";
    const bool mismatch = argc > 2 && std::string(argv[2]) == "mismatch";
    mqas::Context<mqas::core::InitFlags::BOTH> context;
    mqas::io::Context io;
    Engine engine(io);
    auto tls_context = make_psk_context(server, mismatch);
    if (!tls_context) return 5;
    const char* client_config = std::getenv("MQAS_TEST_CLIENT_CONFIG");
    engine.init(server ? SERVER_CONFIG_PATH : (client_config ? client_config : CLIENT_CONFIG_PATH),
                server ? mqas::core::EngineFlags::Server : mqas::core::EngineFlags::None,
                nullptr, tls_context);
    bool security_verified = false;
    bool security_failed = false;
    int accepted_connections = 0;
    if (server) engine.get_engine()->on_new_connect_signal.connect([&](std::shared_ptr<Connection> conn) {
        ++accepted_connections;
        const auto info = conn->security_info();
        security_verified = info.handshake == mqas::core::HandshakeState::Succeeded &&
            info.authentication == mqas::core::PeerAuthentication::ExternalPsk;
    });
    engine.start_recv();
    engine.process_conns();
    auto timer = io.make_shared<mqas::io::Timer>();
    if (!server) {
        sockaddr addr{};
        if (!mqas::io::Ip::str2addr_ipv4("127.0.0.1", 8101, addr)) return 2;
        engine.get_engine()->whitelist_addr.push_back(addr);
        engine.get_engine()->whitelist_port.push_back(8101);
        auto conn = engine.get_engine()->connect(addr, N_LSQVER).lock();
        if (!conn) return 3;
        if (conn->security_info().handshake != mqas::core::HandshakeState::Pending ||
            conn->security_info().authentication != mqas::core::PeerAuthentication::None) return 6;
        conn->on_hsk_done_signal.connect([&](mqas::core::IConnect& c, lsquic_hsk_status) {
            const auto info = c.security_info();
            security_verified = info.handshake == mqas::core::HandshakeState::Succeeded &&
                info.authentication == mqas::core::PeerAuthentication::ExternalPsk;
            security_failed = info.handshake == mqas::core::HandshakeState::Failed &&
                info.authentication == mqas::core::PeerAuthentication::None;
        });
        conn->on_close_signal.connect([&](mqas::core::IConnect& c) {
            const auto info = c.security_info();
            security_failed = info.handshake == mqas::core::HandshakeState::Failed &&
                info.authentication == mqas::core::PeerAuthentication::None;
        });
        const auto rid = conn->make_stream([&](Connection::StreamRequestId request, std::shared_ptr<Variant> stream) {
            if (!request || !stream) return;
            uint8_t data[] = {'o', 'k'};
            stream->req_change<CustomStream>(std::span<uint8_t>(data));
        });
        if (!rid) return 4;
    }
    timer->start([&](mqas::io::Timer* t) { t->stop(); io.stop(); }, 5000, 0);
    io.run();
    bool ok = mismatch ? (!security_verified && (server ? !changed && accepted_connections == 0 : security_failed && !acknowledged)) :
        (security_verified && (server ? changed && accepted_connections == 1 : acknowledged));
    auto retained = engine.get_engine();
    engine.close();
    sockaddr closed_addr{};
    ok = ok && retained->connect(closed_addr, N_LSQVER).expired();
    std::printf("custom %s: %s\n", server ? "server" : "client", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
