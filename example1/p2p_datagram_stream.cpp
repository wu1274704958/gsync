#include "p2p_datagram_stream.hpp"
#include <toml.hpp>
#include <limits>


void P2PDatagramStream::on_connected(const std::string& peer_name)
{
    listen_datagram_received();
    P2PDirectStream<P2PDatagramStream, ChannelControlPair>::on_connected(peer_name);
}

void P2PDatagramStream::on_disconnected(const std::string& reason)
{
    if (_send_channel == ChannelState::Opening || _send_channel == ChannelState::Closing)
        on_channel_result.emit(_send_channel == ChannelState::Opening, false, _channel_request_id, "stream disconnected");
    _send_channel = ChannelState::Closed;
    _recv_channel = false;
    P2PDirectStream<P2PDatagramStream, ChannelControlPair>::on_disconnected(reason);
    if (_on_recv_datagram_connect.connected())
        _on_recv_datagram_connect.disconnect();
}

void P2PDatagramStream::on_connect_failed(mqas::tools::proto::p2p_client::RetCode code)
{
    P2PDirectStream<P2PDatagramStream, ChannelControlPair>::on_connect_failed(code);
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
    constexpr auto prefix = sizeof(uint32_t);
    if (size < prefix || !_connected || !_recv_channel) return;
    uint32_t stream = 0;
    for (size_t i = 0; i < prefix; ++i) stream = (stream << 8) | data[i];
    if (stream != peer_stream_id()) return;
    const std::span<uint8_t> data_span(const_cast<uint8_t*>(data + prefix), size - prefix);
    on_datagram_received_signal.emit(data_span,_peer_id);
}

// ---- send API ----
bool P2PDatagramStream::send_datagram(const std::span<uint8_t>& data)
{
    const auto c = connect.lock();
    if (!c || !_connected || _send_channel != ChannelState::Open || data.size() > static_cast<size_t>(std::numeric_limits<short>::max()) - sizeof(uint32_t))
        return false;
    const auto stream = local_stream_id();
    std::vector<uint8_t> packet(sizeof(uint32_t));
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

void P2PDatagramStream::on_close()
{
    if (_send_channel == ChannelState::Opening || _send_channel == ChannelState::Closing)
        on_channel_result.emit(_send_channel == ChannelState::Opening, false, _channel_request_id, "stream closed");
    _send_channel = ChannelState::Closed;
    _recv_channel = false;
    if (_on_recv_datagram_connect.connected()) _on_recv_datagram_connect.disconnect();
    on_channel_request = {};
    on_channel_result.clear();
    P2PDirectStream<P2PDatagramStream, ChannelControlPair>::on_close();
}

bool P2PDatagramStream::begin_channel_request(bool open, uint32_t request_id)
{
    if (!_connected || (open ? _send_channel != ChannelState::Closed : _send_channel != ChannelState::Open))
        return false;
    _channel_request_id = request_id;
    _send_channel = open ? ChannelState::Opening : ChannelState::Closing;
    return true;
}

void P2PDatagramStream::dispatch_channel_request(bool open, uint32_t request_id)
{
    if (!_connected || _channel_request_id != request_id ||
        _send_channel != (open ? ChannelState::Opening : ChannelState::Closing)) return;
    gsync::ChannelControl request;
    request.set_action(open ? gsync::ChannelControl::OPEN : gsync::ChannelControl::CLOSE);
    request.set_request_id(request_id);
    if (!send<ChannelControlPair>(request)) {
        _send_channel = open ? ChannelState::Closed : ChannelState::Open;
        on_channel_result.emit(open, false, request_id, "failed to send channel request");
    }
}

void P2PDatagramStream::on_read_msg_s(const std::shared_ptr<gsync::ChannelControl>& msg)
{
    if (!_connected || !msg || !msg->has_action()) return;
    using Control = gsync::ChannelControl;
    if (msg->action() == Control::OPEN || msg->action() == Control::CLOSE) {
        const bool open = msg->action() == Control::OPEN;
        std::string reason;
        bool accepted = open ? !_recv_channel : _recv_channel;
        if (!accepted) reason = "channel state conflict";
        if (open && accepted && on_channel_request) accepted = on_channel_request(peer_stream_id(), reason);
        if (!_connected) return;
        if (reason.size() > 1024) reason = "channel rejected";
        Control response;
        response.set_action(open ? Control::OPEN_ACK : Control::CLOSE_ACK);
        response.set_request_id(msg->request_id());
        response.set_accepted(accepted);
        response.set_reason(reason);
        if (send<ChannelControlPair>(response) && accepted) _recv_channel = open;
        return;
    }
    const bool open = msg->action() == Control::OPEN_ACK;
    if ((!open && msg->action() != Control::CLOSE_ACK) ||
        _send_channel != (open ? ChannelState::Opening : ChannelState::Closing) ||
        msg->request_id() != _channel_request_id) return;
    _send_channel = msg->accepted() ? (open ? ChannelState::Open : ChannelState::Closed)
                                    : (open ? ChannelState::Closed : ChannelState::Open);
    on_channel_result.emit(open, msg->accepted(), msg->request_id(), msg->reason());
}
