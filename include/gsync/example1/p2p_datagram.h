#pragma once

#include "gsync/core/types.h"
#include "gsync/example1/lobby.h"
#include "gsync/core/macro.h"

#if __cplusplus
extern "C" {
#endif

typedef struct GSYNC_EXTERN GSY_P2PDatagramConnectionContext
{
    void(*on_connected)(ErrorCode, GSY_StreamId, GSY_PeerId,const char* peer_name, GSY_RequestId);
    void(*on_datagram_received)(const uint8_t*, size_t, GSY_StreamId, GSY_PeerId,GSY_ConnectionHwnd);
    void(*on_disconnected)(ErrorCode, GSY_StreamId, GSY_PeerId, const char* reason);
    void(*on_connect_failed)(ErrorCode, GSY_StreamId, GSY_PeerId, GSY_RequestId);
    void(*on_error)(GSY_ConnectionHwnd, GSY_StreamId, ErrorCode, const char*, GSY_RequestId);
    uint32_t self_id;
    const char* verify_token; // Reserved for ABI compatibility; ignored by authenticated P2P.
    int verify_token_len;
    int use_relay; // 0 = direct connection, 1 = relay connection; optional for client side, ignored for server side
    int is_server;            // 0 = client (initiator), 1 = server (acceptor)
    void* extend;
    uint8_t _check_code; // do not modify
    // Optional rejection detail; reason is valid only during the callback.
    void (*on_stream_rejected)(GSY_StreamId, const char* reason, GSY_RequestId);
    /* Return nonzero to accept; reason must remain valid after the callback returns. */
    int (*on_channel_request)(GSY_ConnectionHwnd, GSY_StreamId, GSY_P2PStreamId, const char** reason);
    /* opening is nonzero for open, zero for close. reason is valid during the callback. */
    void (*on_channel_result)(GSY_StreamId, int opening, ErrorCode, GSY_RequestId, const char* reason);
} GSY_P2PDatagramConnectionContext;

// Completion runs on the SDK I/O thread, possibly before this function returns.
// The context must remain alive until the connection and its callbacks end.
typedef void (*GSY_DatagramStreamCreated)(ErrorCode, GSY_StreamId, GSY_RequestId, void*);
/* Creates a direct datagram stream on an authenticated connection. The callback
 * runs exactly once for an accepted request and may run before return. The
 * context remains valid until the stream and all its callbacks have ended. */
ErrorCode GSYNC_EXTERN GSY_MakePeerDatagramStream(GSY_ConnectionHwnd, GSY_P2PStreamId,
    GSY_P2PDatagramConnectionContext*, GSY_RequestId, GSY_DatagramStreamCreated, void*);
ErrorCode GSYNC_EXTERN GSY_ConnectPeerDatagram(GSY_ConnectionHwnd handle, GSY_PeerId, GSY_P2PStreamId local_stream_id,
    GSY_P2PDatagramConnectionContext*,GSY_RequestId, GSY_DatagramStreamCreated, void*);
// Only established streams are visible. Identifiers are scoped to the connection.
ErrorCode GSYNC_EXTERN GSY_FindPeerStream(GSY_ConnectionHwnd, GSY_P2PStreamId peer_stream_id, GSY_StreamId*);
ErrorCode GSYNC_EXTERN GSY_GetPeerStreamIds(GSY_ConnectionHwnd, GSY_StreamId,
    GSY_P2PStreamId* local_stream_id, GSY_P2PStreamId* peer_stream_id);
ErrorCode GSYNC_EXTERN GSY_SendDatagram(GSY_ConnectionHwnd handle,GSY_StreamId sid,const char* data,size_t size,GSY_RequestId request_id);
ErrorCode GSYNC_EXTERN GSY_OpenDatagramChannel(GSY_ConnectionHwnd, GSY_StreamId, GSY_RequestId);
ErrorCode GSYNC_EXTERN GSY_CloseDatagramChannel(GSY_ConnectionHwnd, GSY_StreamId, GSY_RequestId);
ErrorCode GSYNC_EXTERN GSY_ReqDisconnectPeerDatagram(GSY_ConnectionHwnd handle,GSY_StreamId sid,const char* reason,GSY_RequestId request_id);

ErrorCode GSYNC_EXTERN GSY_ServerConnectPeerDatagram(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,GSY_P2PDatagramConnectionContext* context,GSY_RequestId request_id);

#if __cplusplus
}
#endif
