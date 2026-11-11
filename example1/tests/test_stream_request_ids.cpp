#include "../lobby.h"
#include "../lib.hpp"
#include <mqas/context.h>
#include <mqas/io/context.h>
#include <mqas/io/timer.h>
#include <mqas/io/ip.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "normal";
    const bool cancel = mode == "cancel" || mode == "reentrant";
    const bool cancel_all = mode == "cancel_all";
    const bool cancel_from_success = mode == "success_cancel";
    mqas::Context<mqas::core::InitFlags::BOTH> context;
    mqas::io::Context io;
    P2PDatagramEngine engine(io);
    engine.init(CONFIG_PATH, mqas::core::EngineFlags::None);
    engine.start_recv();
    engine.process_conns();
    sockaddr peer{};
    if (!mqas::io::Ip::str2addr_ipv4("127.0.0.1", 8101, peer)) return 2;
    engine.get_engine()->whitelist_addr.push_back(peer);
    engine.get_engine()->whitelist_port.push_back(8101);
    auto conn = engine.get_engine()->connect(peer, N_LSQVER).lock();
    if (!conn) return 3;
    using Conn = mqas::core::Connect<mqas::core::StreamVariant<P2PDatagramStreamPair>>;
    using RID = Conn::StreamRequestId;
    std::vector<RID> completed;
    std::vector<RID> failed;
    RID reentrant_request = 0;
    bool reenter = false;
    std::function<RID()> request;
    request = [&] {
        return conn->make_stream([&](RID rid, std::shared_ptr<Conn::StreamType> stream) {
            if (stream) completed.push_back(rid);
            else failed.push_back(rid);
            if (stream && cancel_from_success && completed.size() == 1)
                conn->cancel_pending_streams(99);
            if (stream && completed.size() == ((cancel_all || cancel_from_success) ? 1u : 3u)) io.stop();
            if (!stream && reenter) {
                reenter = false;
                reentrant_request = request();
            }
            std::printf("completion rid=%llu success=%d\n", (unsigned long long)rid, stream != nullptr);
        });
    };
    const auto a = request();
    const auto b = request();
    const RID c = request();
    const auto before = conn->pending_streams();
    if (before != 3) { std::printf("expected 3 delayed requests, got %u\n", before); return 4; }
    RID d = 0;
    if (conn->cancel_pending_streams(0) != 3 || !failed.empty()) return 7;
    if (cancel) {
        reenter = mode == "reentrant";
        const auto remaining = conn->cancel_pending_streams(1);
        if (remaining != 2 || (mode == "reentrant" && !reentrant_request)) return 5;
        if (failed != std::vector<RID>{c}) return 6;
        d = reentrant_request ? reentrant_request : request();
    } else if (cancel_all) {
        // Exercise cancellation through the public base-class interface.
        mqas::core::IConnect& base = *conn;
        if (base.cancel_pending_streams(99) != 0 || failed != std::vector<RID>{a, b, c}) return 8;
        d = request();
    }
    auto timer = io.make_shared<mqas::io::Timer>();
    timer->start([&](mqas::io::Timer* t) { t->stop(); io.stop(); }, 8000, 0);
    io.run();
    if (timer->is_start()) timer->stop();
    const std::vector<RID> expected = cancel ? std::vector<RID>{a, b, d} :
        cancel_all ? std::vector<RID>{d} : cancel_from_success ? std::vector<RID>{a} : std::vector<RID>{a, b, c};
    const std::vector<RID> expected_failed = cancel ? std::vector<RID>{c} :
        cancel_all ? std::vector<RID>{a, b, c} : cancel_from_success ? std::vector<RID>{b, c} : std::vector<RID>{};
    const bool ok = completed == expected && failed == expected_failed;
    std::printf("result=%s mode=%s\n", ok ? "PASS" : "FAIL", mode.c_str());
    engine.close();
    return ok ? 0 : 1;
}
