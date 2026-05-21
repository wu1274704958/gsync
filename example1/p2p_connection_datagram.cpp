#include "p2p_connection_datagram.h"
#include "lib.hpp"
#include "../src/common.h"
#include "mqas/tools/stream/MsgDef.h"
#include <easylogging++.h>

using P2PDatagramStreamVariant = mqas::core::StreamVariant<P2PDatagramStreamPair>;
using P2PDatagramConnect       = mqas::core::Connect<P2PDatagramStreamVariant>;

// ── wire C callbacks onto P2PDatagramStream signals ──────────────────────────
static void connect_datagram_signals(
    const std::shared_ptr<P2PDatagramStream>& ds,
    GSY_P2PDatagramConnectionContext* ctx,
    GSY_ConnectionHwnd handle,
    GSY_StreamId stream_id,
    GSY_RequestId request_id)
{
    ds->on_connected_signal.connect(
        [ctx, stream_id,request_id](const std::string& peer_name,uint32_t peer_id) {
        if (ctx->on_connected)
            ctx->on_connected(EC_Ok, stream_id, peer_id, peer_name.c_str(), request_id);
    });
    ds->on_disconnected_signal.connect(
        [ctx, stream_id](const std::string& reason,uint32_t peer_id) {
        if (ctx->on_disconnected)
            ctx->on_disconnected(EC_Ok, stream_id, peer_id, reason.c_str());
    });
    ds->on_connect_failed_signal.connect(
        [ctx, stream_id, request_id](mqas::tools::proto::p2p_client::RetCode,uint32_t peer_id) {
        if (ctx->on_connect_failed)
            ctx->on_connect_failed(EC_Fail, stream_id, peer_id, request_id);
    });
    ds->on_datagram_received_signal.connect(
        [ctx, stream_id, handle](const std::span<uint8_t>& data,uint32_t peer_id) {
        if (ctx->on_datagram_received)
            ctx->on_datagram_received(data.data(), data.size(), stream_id, peer_id, handle);
    });
}

// ── GSY_ConnectPeerDatagram ───────────────────────────────────────────────────
// Client side only (is_server ignored for now).
// Mirrors main.cpp on_new_p2p_connect(client) + on_new_p2p_stream(client):
//   make_stream → req_change<P2PDatagramStream, ReqDirectConnectPair> →
//   get_holds_stream → connect_datagram_signals → return stream_id
GSY_StreamId GSY_ConnectPeerDatagram(GSY_ConnectionHwnd handle, GSY_PeerId peer_id,
                                      GSY_P2PDatagramConnectionContext* context,
                                      GSY_RequestId request_id)
{
    context->_check_code = 193;

    std::shared_ptr<mqas::core::IConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        if (!connect_map.contains(handle) || connect_map.at(handle).expired())
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_InvalidHandler,
                                  "Invalid connection handle", request_id);
            return INVALID_SID;
        }
        conn = connect_map.at(handle).lock();
        if (!conn)
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_InvalidHandler,
                                  "Connection already disconnected", request_id);
            return INVALID_SID;
        }
    }

    return push_task_with_result<GSY_StreamId>(
        [conn, context, peer_id, handle, request_id]() -> GSY_StreamId
    {
        const auto p2p_conn = std::static_pointer_cast<P2PDatagramConnect>(conn);

        mqas::comm::locator::inst()->deposit_cxt<LocalPeerId>(p2p_conn,context->self_id);

        // make_stream — same synchronous-wait pattern as GSY_RegisterToLobby /
        // main.cpp on_new_p2p_connect client: conn->make_stream(...)
        std::shared_ptr<P2PDatagramStreamVariant> stream_variant = nullptr;
        std::atomic_bool stream_ready = false;
        sigc::connection sig = p2p_conn->make_stream(
            [&stream_variant, &stream_ready](std::shared_ptr<P2PDatagramStreamVariant> s) {
            stream_variant = std::move(s);
            stream_ready.store(true, std::memory_order_release);
        });
        while (!stream_ready.load(std::memory_order_acquire)) {}
        sig.disconnect();

        if (!stream_variant)
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_MakeStreamFailed,
                                  "make_stream failed", request_id);
            return INVALID_SID;
        }

        const GSY_StreamId stream_id =
            reinterpret_cast<GSY_StreamId>(stream_variant->get_origin());

        // Build RequestDirectConnect with verify_token if provided.
        // Mirrors main.cpp on_new_p2p_stream client: token->set_data(helper_result->verify_token().data())
        mqas::tools::proto::p2p_client::RequestDirectConnect req;
        if (context->verify_token && context->verify_token_len > 0)
            req.mutable_token()->set_data(context->verify_token,
                static_cast<size_t>(context->verify_token_len));

        // req_change transitions the stream to P2PDatagramStream state.
        // Mirrors main.cpp: stream->req_change<P2PChatStream, ReqDirectConnectPair>(req)
        if (!stream_variant->req_change<P2PDatagramStream,
                                        mqas::tools::p2p_direct::ReqDirectConnectPair>(req))
        {
            if (context->on_error)
                context->on_error(handle, stream_id, EC_MakeStreamFailed,
                                  "req_change failed", request_id);
            stream_variant->close();
            return INVALID_SID;
        }

        // get_holds_stream — mirrors main.cpp: stream->get_holds_stream<P2PChatStream>()
        const auto ds = stream_variant->get_holds_stream<P2PDatagramStream>();
        if (!ds)
        {
            if (context->on_error)
                context->on_error(handle, stream_id, EC_MakeStreamFailed,
                                  "get_holds_stream failed", request_id);
            return INVALID_SID;
        }

        connect_datagram_signals(ds, context, handle, stream_id, request_id);
        return stream_id;
    });
}

// ── GSY_SendDatagram ──────────────────────────────────────────────────────────
ErrorCode GSY_SendDatagram(GSY_ConnectionHwnd handle, GSY_StreamId sid,
                            const char* data, size_t size, GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    push_task([conn, sid,
               data_vec = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data),
                                               reinterpret_cast<const uint8_t*>(data) + size)]()
    {
        const auto p2p_conn = std::static_pointer_cast<P2PDatagramConnect>(conn);
        const auto sv = p2p_conn->get_stream(reinterpret_cast<::lsquic_stream_t*>(sid));
        if (!sv)
        {
            LOG(WARNING) << "GSY_SendDatagram: stream not found sid=" << sid;
            return;
        }
        const auto ds = sv->get_holds_stream<P2PDatagramStream>();
        if (!ds)
        {
            LOG(WARNING) << "GSY_SendDatagram: P2PDatagramStream not ready sid=" << sid;
            return;
        }
        ds->send_datagram(const_cast<uint8_t*>(data_vec.data()), data_vec.size());
    });
    return EC_Pending;
}

// ── GSY_ReqDisconnectPeerDatagram ─────────────────────────────────────────────
ErrorCode GSY_ReqDisconnectPeerDatagram(GSY_ConnectionHwnd handle, GSY_StreamId sid,
                                         const char* reason, GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    push_task([conn, sid,
               reason_str = reason ? std::string(reason) : std::string()]()
    {
        const auto p2p_conn = std::static_pointer_cast<P2PDatagramConnect>(conn);
        const auto sv = p2p_conn->get_stream(reinterpret_cast<::lsquic_stream_t*>(sid));
        if (!sv)
        {
            LOG(WARNING) << "GSY_ReqDisconnectPeerDatagram: stream not found sid=" << sid;
            return;
        }
        const auto ds = sv->get_holds_stream<P2PDatagramStream>();
        if (!ds)
        {
            LOG(WARNING) << "GSY_ReqDisconnectPeerDatagram: P2PDatagramStream not ready sid=" << sid;
            return;
        }
        ds->req_quit(reason_str);
    });
    return EC_Pending;
}

// ── GSY_ServerConnectPeerDatagram ─────────────────────────────────────────────
// Server side: hook on_new_stream_signal on the connection.
// When a client opens a stream and req_change succeeds, on_change_stream_signal
// fires → cast to P2PDatagramStream → wire signals.
// peer_id (out): filled with 0 now; peer identity is available in on_connected peer_name.
ErrorCode GSY_ServerConnectPeerDatagram(GSY_ConnectionHwnd handle,
                                            GSY_StreamId stream_id,
                                            GSY_P2PDatagramConnectionContext* context,
                                            GSY_RequestId request_id)
{
    context->_check_code = 193;

    std::shared_ptr<mqas::core::IConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        if (!connect_map.contains(handle) || connect_map.at(handle).expired())
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_InvalidHandler,
                                  "Invalid connection handle", request_id);
            return EC_InvalidHandler;
        }
        conn = connect_map.at(handle).lock();
        if (!conn)
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_InvalidHandler,
                                  "Connection already disconnected", request_id);
            return EC_InvalidHandler;
        }
    }

    // Push into main loop — all lsquic / sigc++ calls must happen on the main thread
    return push_task_with_result<ErrorCode>([conn, context, handle, request_id, stream_id]() -> ErrorCode
    {
        const auto p2p_conn = std::static_pointer_cast<P2PDatagramConnect>(conn);

        mqas::comm::locator::inst()->deposit_cxt<LocalPeerId>(p2p_conn,context->self_id);

        auto stream = p2p_conn->get_stream(reinterpret_cast<::lsquic_stream*>(stream_id));
        if (!stream)
        {
            if (context->on_error)
                context->on_error(handle, INVALID_SID, EC_InvalidStream,
                                  "Not found stream", request_id);
            return EC_InvalidStream;
        }

        stream->on_change_stream_signal.connect([context,handle,stream_id,request_id](std::shared_ptr<mqas::core::IStreamVariant> s)
        {
            auto ds = std::dynamic_pointer_cast<P2PDatagramStream>(s);
            if (ds)
            {
                LOG(INFO) << "stream changed to P2PDatagramStream, sid=" << reinterpret_cast<GSY_StreamId>(s->get_origin());
                connect_datagram_signals(ds, context, handle, stream_id, request_id);
            }
        });
        return EC_Ok;
    });
}
