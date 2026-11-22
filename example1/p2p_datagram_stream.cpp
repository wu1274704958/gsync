#include "p2p_datagram_stream.hpp"
#include <toml.hpp>
#include <limits>


void P2PDatagramStream::on_connected(const std::string& peer_name)
{
    listen_datagram_received();
    P2PDirectStream<P2PDatagramStream>::on_connected(peer_name);
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
    constexpr auto prefix = sizeof(uint64_t);
    if (size < prefix || !_connected) return;
    uint64_t stream = 0;
    for (size_t i = 0; i < prefix; ++i) stream = (stream << 8) | data[i];
    if (stream != lsquic_stream_id(get_origin())) return;
    const std::span<uint8_t> data_span(const_cast<uint8_t*>(data + prefix), size - prefix);
    on_datagram_received_signal.emit(data_span,_peer_id);
}

// ---- send API ----
bool P2PDatagramStream::send_datagram(const std::span<uint8_t>& data)
{
    const auto c = connect.lock();
    if (!c || !_connected || data.size() > static_cast<size_t>(std::numeric_limits<short>::max()) - sizeof(uint64_t))
        return false;
    const auto stream = lsquic_stream_id(get_origin());
    std::vector<uint8_t> packet(sizeof(uint64_t));
    for (size_t i = 0; i < packet.size(); ++i)
        packet[i] = static_cast<uint8_t>(stream >> (8 * (packet.size() - i - 1)));
    packet.insert(packet.end(), data.begin(), data.end());
    return c->write_datagram(packet) && c->flush_datagram();
}

bool P2PDatagramStream::send_datagram(uint8_t* data, size_t size)
{
    const std::span<uint8_t> data_span(data, size);
    return send_datagram(data_span);
}
