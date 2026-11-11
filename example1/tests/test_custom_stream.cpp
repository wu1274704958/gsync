#include <mqas/context.h>
#include <mqas/io/context.h>
#include <mqas/io/timer.h>
#include <mqas/io/ip.h>
#include <mqas/core/connect.h>
#include <mqas/core/stream.h>
#include <mqas/core/sub_engine.h>
#include <cstdio>
#include <string>

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
    mqas::Context<mqas::core::InitFlags::BOTH> context;
    mqas::io::Context io;
    Engine engine(io);
    engine.init(server ? SERVER_CONFIG_PATH : CLIENT_CONFIG_PATH,
                server ? mqas::core::EngineFlags::Server : mqas::core::EngineFlags::None);
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
        const auto rid = conn->make_stream([&](Connection::StreamRequestId request, std::shared_ptr<Variant> stream) {
            if (!request || !stream) return;
            uint8_t data[] = {'o', 'k'};
            stream->req_change<CustomStream>(std::span<uint8_t>(data));
        });
        if (!rid) return 4;
    }
    timer->start([&](mqas::io::Timer* t) { t->stop(); io.stop(); }, 5000, 0);
    io.run();
    const bool ok = server ? changed : acknowledged;
    std::printf("custom %s: %s\n", server ? "server" : "client", ok ? "PASS" : "FAIL");
    engine.close();
    return ok ? 0 : 1;
}
