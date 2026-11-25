#pragma once

#include <mqas/core/pb_stream.h>
#include "p2p_datagram.pb.h"
#include <sigc++/sigc++.h>
#include <mqas/tools/stream/p2p_direct_stream.h>


using ChannelControlPair = mqas::core::PBMsgPair<5, gsync::ChannelControl>;
class P2PDatagramStream : public mqas::tools::p2p_direct::P2PDirectStream<P2PDatagramStream, ChannelControlPair>
{
public:
    ~P2PDatagramStream() { _on_recv_datagram_connect.disconnect(); }
    std::function<bool(uint32_t, std::string&)> on_channel_request;
    sigc::signal<void(bool, bool, uint32_t, const std::string&)> on_channel_result;
    void on_read_msg_s(const std::shared_ptr<gsync::ChannelControl>&);
    bool begin_channel_request(bool open, uint32_t request_id);
    void dispatch_channel_request(bool open, uint32_t request_id);
    bool can_send_datagram() const { return _connected && _send_channel == ChannelState::Open; }
    void on_close();
    sigc::signal<void(const std::span<uint8_t>&,uint32_t)> on_datagram_received_signal;
    bool send_datagram(const std::span<uint8_t>& data);
    bool send_datagram(uint8_t* data, size_t size);
protected:
    void on_connected(const std::string& peer_name) override;
    void on_disconnected(const std::string& reason) override;
    void on_connect_failed(mqas::tools::proto::p2p_client::RetCode code) override;

    void listen_datagram_received();
    void on_datagram_received(const uint8_t*, size_t);

protected:
    sigc::connection _on_recv_datagram_connect;
    enum class ChannelState { Closed, Opening, Open, Closing };
    ChannelState _send_channel = ChannelState::Closed;
    bool _recv_channel = false;
    uint32_t _channel_request_id = 0;

};
