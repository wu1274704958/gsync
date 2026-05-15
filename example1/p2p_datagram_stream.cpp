#include "p2p_datagram_stream.hpp"
#include <toml.hpp>

// ---- initiator side: fill in the request before sending ----
mqas::core::StreamVariantErrcode P2PDatagramStream::on_local_change_msg_s(
    const std::shared_ptr<gsync::ReqConnectDatagram>& msg,
    std::vector<uint8_t>& ret_buf)
{
    auto engine = connect_cxt_->engine_cxt_->engine.lock();
    auto name = toml::find<std::string>(*engine->get_config(), "p2p", "name");
    if (name.empty())
        return mqas::core::StreamVariantErrcode::failed;
    msg->set_name(std::move(name));
    mqas::core::ProtoBufMsg::write_msg<ReqConnectDatagramPair>(ret_buf, *msg);
    return mqas::core::StreamVariantErrcode::ok;
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


// ---- initiator side: receive ack from responder ----
void P2PDatagramStream::on_peer_change_ack_msg_s(
    mqas::core::StreamVariantErrcode code,
    const std::shared_ptr<gsync::RespondConnectDatagram>& msg)
{
    if (code == mqas::core::StreamVariantErrcode::ok)
    {
        _peer_name = msg->name();
        listen_datagram_received();
        on_connected_signal.emit(_peer_name);
    }
}

// ---- responder side: receive the request and reply ----
mqas::core::StreamVariantErrcode P2PDatagramStream::on_change_msg_s(
    const std::shared_ptr<gsync::ReqConnectDatagram>& msg,
    std::vector<uint8_t>& ret_buf)
{
    _peer_name = msg->name();

    auto engine = connect_cxt_->engine_cxt_->engine.lock();
    auto local_name = toml::find<std::string>(*engine->get_config(), "p2p", "name");

    listen_datagram_received();
    on_connected_signal.emit(_peer_name);

    gsync::RespondConnectDatagram resp;
    resp.set_name(std::move(local_name));
    mqas::core::ProtoBufMsg::write_msg<RespondConnectDatagramPair>(ret_buf, resp);
    return mqas::core::StreamVariantErrcode::ok;
}

void P2PDatagramStream::on_close()
{
    ProtoBufStream::on_close();
    on_stream_closed_signal.emit();
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
