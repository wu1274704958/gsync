#pragma once

#include <mqas/core/pb_stream.h>
#include "p2p_datagram.pb.h"
#include <sigc++/sigc++.h>
#include <vector>
#include <cstdint>

using ReqConnectDatagramPair    = mqas::core::PBMsgPair<1, gsync::ReqConnectDatagram>;
using RespondConnectDatagramPair = mqas::core::PBMsgPair<2, gsync::RespondConnectDatagram>;
using DatagramPair              = mqas::core::PBMsgPair<3, gsync::Datagram>;

class P2PDatagramStream : public mqas::core::ProtoBufStream<P2PDatagramStream,
    ReqConnectDatagramPair, RespondConnectDatagramPair, DatagramPair>
{
public:
    // 连接成功 (本端发起或接受), 参数为对端 name
    sigc::signal<void(const std::string&)> on_connected_signal;
    // 收到数据报, 参数为原始字节
    sigc::signal<void(const std::span<uint8_t>&)> on_datagram_received_signal;
    // stream 关闭
    sigc::signal<void()> on_stream_closed_signal;
    sigc::connection _on_recv_datagram_connect;

    // --- ProtoBufStream 回调 (initiator 侧) ---
    mqas::core::StreamVariantErrcode on_local_change_msg_s(
        const std::shared_ptr<gsync::ReqConnectDatagram>& msg,
        std::vector<uint8_t>& ret_buf);

    void on_peer_change_ack_msg_s(
        mqas::core::StreamVariantErrcode code,
        const std::shared_ptr<gsync::RespondConnectDatagram>& msg);

    // --- ProtoBufStream 回调 (responder 侧) ---
    mqas::core::StreamVariantErrcode on_change_msg_s(
        const std::shared_ptr<gsync::ReqConnectDatagram>& msg,
        std::vector<uint8_t>& ret_buf);

    void on_close();

    // --- 公开 API ---
    bool send_datagram(const std::span<uint8_t>& data);
    bool send_datagram(uint8_t* data, size_t size);

    std::string get_peer_name() const { return _peer_name; }

protected:
    void listen_datagram_received();
    void on_datagram_received(const uint8_t*, size_t);

private:
    std::string _peer_name;
    bool _connected:1 = false;
};
