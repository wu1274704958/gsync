#pragma once

#include <mqas/core/pb_stream.h>
#include "p2p_datagram.pb.h"
#include <sigc++/sigc++.h>
#include <mqas/tools/stream/p2p_direct_stream.h>


class P2PDatagramStream : public mqas::tools::p2p_direct::P2PDirectStream<P2PDatagramStream>
{
public:
    sigc::signal<void(const std::span<uint8_t>&)> on_datagram_received_signal;
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

};
