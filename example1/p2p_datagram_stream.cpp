#include "p2p_datagram_stream.hpp"
#include <toml.hpp>


void P2PDatagramStream::on_connected(const std::string& peer_name)
{
    P2PDirectStream<P2PDatagramStream>::on_connected(peer_name);
    listen_datagram_received();
}

void P2PDatagramStream::on_disconnected(const std::string& reason)
{
    P2PDirectStream<P2PDatagramStream>::on_disconnected(reason);
    if (_on_recv_datagram_connect.connected())
        _on_recv_datagram_connect.disconnect();
}

void P2PDatagramStream::on_connect_failed(mqas::tools::proto::p2p_client::RetCode code)
{
    P2PDirectStream<P2PDatagramStream>::on_connect_failed(code);
}

void P2PDatagramStream::listen_datagram_received()
{
    if (_on_recv_datagram_connect.connected())
        return;
    const auto c = connect.lock();
    if (!c)
    {
        LOG(WARNING) << "Failed to lock connect in listen_datagram_received";
        return;
    }
    _on_recv_datagram_connect = c->on_recv_datagram_signal.connect(sigc::mem_fun(*this, &P2PDatagramStream::on_datagram_received));
}

void P2PDatagramStream::on_datagram_received(const uint8_t* data, size_t size)
{
    const std::span<uint8_t> data_span(const_cast<uint8_t*>(data), size);
    on_datagram_received_signal.emit(data_span);
}

// ---- send API ----
bool P2PDatagramStream::send_datagram(const std::span<uint8_t>& data)
{
    const auto c = connect.lock();
    if (!c)
        return false;
    return c->write_datagram(data);
}

bool P2PDatagramStream::send_datagram(uint8_t* data, size_t size)
{
    const std::span<uint8_t> data_span(data, size);
    return send_datagram(data_span);
}
