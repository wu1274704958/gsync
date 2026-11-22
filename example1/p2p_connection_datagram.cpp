#include "p2p_connection_datagram.h"
#include "lib.hpp"
#include "p2p_peer_internal.h"
#include "../src/common.h"
#include "mqas/tools/stream/MsgDef.h"
#include <easylogging++.h>

using P2PDatagramStreamVariant = mqas::core::StreamVariant<P2PDatagramStreamPair>;
using P2PDatagramConnect       = mqas::core::Connect<P2PDatagramStreamVariant>;

static bool is_datagram_handle(GSY_ConnectionHwnd handle) {
    const auto slot = handle / MAX_CONNECTION_HWND;
    return slot == 2 || (slot >= SERVER_ENGINE_ID_OFFSET + 2 * MAX_SERVER_HWND &&
        slot < SERVER_ENGINE_ID_OFFSET + 3 * MAX_SERVER_HWND);
}

// ── wire C callbacks onto P2PDatagramStream signals ──────────────────────────
static void connect_datagram_signals(
    const std::shared_ptr<P2PDatagramStream>& ds,
    GSY_P2PDatagramConnectionContext* ctx,
    GSY_ConnectionHwnd handle,
    GSY_StreamId stream_id,
    GSY_RequestId request_id)
{
    ds->on_rejected_signal.connect([ctx, stream_id, request_id](const std::string& reason) {
        if (ctx->on_stream_rejected) ctx->on_stream_rejected(stream_id, reason.c_str(), request_id);
    });
    ds->on_connected_signal.connect(
        [ctx, handle, stream_id,request_id](const std::string& peer_name,uint32_t peer_id) {
        P2PCredentials credentials{};
        if (p2p_peer_credentials(handle, credentials)) peer_id = credentials.other;
        if (ctx->on_connected)
            ctx->on_connected(EC_Ok, stream_id, peer_id, peer_name.c_str(), request_id);
    });
    ds->on_disconnected_signal.connect(
        [ctx, handle, stream_id](const std::string& reason,uint32_t peer_id) {
        P2PCredentials credentials{};
        if (p2p_peer_credentials(handle, credentials)) peer_id = credentials.other;
        if (ctx->on_disconnected)
            ctx->on_disconnected(EC_Ok, stream_id, peer_id, reason.c_str());
    });
    ds->on_connect_failed_signal.connect(
        [ctx, handle, stream_id, request_id](mqas::tools::proto::p2p_client::RetCode,uint32_t peer_id) {
        P2PCredentials credentials{};
        if (p2p_peer_credentials(handle, credentials)) peer_id = credentials.other;
        if (ctx->on_connect_failed)
            ctx->on_connect_failed(EC_Fail, stream_id, peer_id, request_id);
    });
    ds->on_datagram_received_signal.connect(
        [ctx, stream_id, handle](const std::span<uint8_t>& data,uint32_t peer_id) {
        P2PCredentials credentials{};
        if (p2p_peer_credentials(handle, credentials)) peer_id = credentials.other;
        if (ctx->on_datagram_received)
            ctx->on_datagram_received(data.data(), data.size(), stream_id, peer_id, handle);
    });
}

// ── GSY_ConnectPeerDatagram ───────────────────────────────────────────────────
// Opens a direct stream on an authenticated transport.
ErrorCode GSY_ConnectPeerDatagram(GSY_ConnectionHwnd handle, GSY_PeerId peer_id, GSY_P2PStreamId local_stream_id,
    GSY_P2PDatagramConnectionContext* context, GSY_RequestId request_id,
    GSY_DatagramStreamCreated completion, void* user)
{
    if (!context || !completion || !local_stream_id) return EC_Fail;
    std::shared_ptr<mqas::core::IConnect> conn;
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        const auto it = connect_map.find(handle);
        if (it != connect_map.end()) conn = it->second.lock();
    }
    if (!conn) return EC_InvalidHandler;
    if (!is_datagram_handle(handle) ||
        conn->security_info().authentication != mqas::core::PeerAuthentication::ExternalPsk)
        return EC_EngineNotMatch;
    auto p2p_conn = std::static_pointer_cast<P2PDatagramConnect>(conn);
    P2PCredentials credentials{};
    if (!p2p_peer_credentials(handle, credentials)) {
        const bool found = push_task_with_result<bool>([conn, &credentials] {
            const auto locator = mqas::comm::locator::inst();
            const auto self = locator->get<LocalPeerId>(conn);
            const auto result = locator->get<std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectResult>>(conn);
            if (!self || !result || !result->get()) return false;
            credentials = {self->get(), result->get()->peer_id()};
            return true;
        });
        if (!found) return EC_EngineNotMatch;
    }
    peer_id = credentials.other;
    context->_check_code = 193;
    const auto self_id = credentials.self;
    push_task([p2p_conn, context, peer_id, handle, request_id, completion, user,
               self_id, local_stream_id]() {
        if (p2p_conn->is_closed()) { completion(EC_Disconnected, INVALID_SID, request_id, user); return; }
        mqas::comm::locator::inst()->deposit_cxt<LocalPeerId>(p2p_conn, self_id);
        const auto rid = p2p_conn->make_stream(
            [context, peer_id, handle, request_id, completion, user, local_stream_id]
            (P2PDatagramConnect::StreamRequestId, std::shared_ptr<P2PDatagramStreamVariant> stream) {
                if (!stream) { completion(EC_MakeStreamFailed, INVALID_SID, request_id, user); return; }
                const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
                mqas::tools::proto::p2p_client::RequestDirectConnect req;
                req.set_peer_id(peer_id);
                req.set_stream_id(local_stream_id);
                if (!stream->req_change<P2PDatagramStream, mqas::tools::p2p_direct::ReqDirectConnectPair>(req)) {
                    stream->close();
                    completion(EC_MakeStreamFailed, INVALID_SID, request_id, user);
                    return;
                }
                const auto ds = stream->get_holds_stream<P2PDatagramStream>();
                if (!ds) { stream->close(); completion(EC_MakeStreamFailed, INVALID_SID, request_id, user); return; }
                connect_datagram_signals(ds, context, handle, sid, request_id);
                completion(EC_Ok, sid, request_id, user);
            });
        if (!rid) completion(EC_MakeStreamFailed, INVALID_SID, request_id, user);
    });
    return EC_Pending;
}

// ── GSY_SendDatagram ──────────────────────────────────────────────────────────
ErrorCode GSY_SendDatagram(GSY_ConnectionHwnd handle, GSY_StreamId sid,
                            const char* data, size_t size, GSY_RequestId request_id)
{
    if (!is_datagram_handle(handle)) return EC_EngineNotMatch;
    if (!data || !size) return EC_Fail;
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
    if (!is_datagram_handle(handle)) return EC_EngineNotMatch;
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
// Bind the per-stream callbacks before the inbound direct handshake runs.
ErrorCode GSY_ServerConnectPeerDatagram(GSY_ConnectionHwnd handle,
                                            GSY_StreamId stream_id,
                                            GSY_P2PDatagramConnectionContext* context,
                                            GSY_RequestId request_id)
{
    if (!context) return EC_Fail;
    if (!is_datagram_handle(handle)) return EC_EngineNotMatch;
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

ErrorCode GSY_FindPeerStream(GSY_ConnectionHwnd handle, GSY_P2PStreamId peer_stream_id, GSY_StreamId* out) {
    if (!out || !peer_stream_id) return EC_Fail;
    if (!is_running) return EC_NotInitialized;
    *out = INVALID_SID;
    return push_task_with_result<ErrorCode>([handle, peer_stream_id, out] {
        std::shared_ptr<mqas::core::IConnect> conn;
        { std::lock_guard lock(connect_mutex); auto it = connect_map.find(handle); if (it != connect_map.end()) conn = it->second.lock(); }
        if (!conn) return EC_InvalidHandler;
        auto registry = mqas::comm::locator::inst()->get<std::shared_ptr<mqas::tools::p2p_direct::DirectStreamRegistry>>(conn);
        void* stream = nullptr;
        if (!registry || !registry.value().get()->find_peer(peer_stream_id, stream)) return EC_NotExists;
        *out = reinterpret_cast<GSY_StreamId>(stream); return EC_Ok;
    });
}
ErrorCode GSY_GetPeerStreamIds(GSY_ConnectionHwnd handle, GSY_StreamId sid, GSY_P2PStreamId* local, GSY_P2PStreamId* peer) {
    if (!local || !peer) return EC_Fail;
    if (!is_running) return EC_NotInitialized;
    *local = *peer = 0;
    return push_task_with_result<ErrorCode>([handle, sid, local, peer] {
        std::shared_ptr<mqas::core::IConnect> conn;
        { std::lock_guard lock(connect_mutex); auto it = connect_map.find(handle); if (it != connect_map.end()) conn = it->second.lock(); }
        if (!conn) return EC_InvalidHandler;
        auto registry = mqas::comm::locator::inst()->get<std::shared_ptr<mqas::tools::p2p_direct::DirectStreamRegistry>>(conn);
        if (!registry || !registry.value().get()->find_stream(reinterpret_cast<void*>(sid), *local, *peer)) return EC_NotExists;
        return EC_Ok;
    });
}
