#pragma once

#include "def.h"
#include "lobby.h"
#include "macro.h"

#if __cplusplus
extern "C" {
#endif

struct GSYNC_EXTERN GSY_P2PDatagramConnectionContext
{
    void(*on_connected)(ErrorCode, GSY_StreamId, GSY_PeerId,const char* peer_name, GSY_RequestId);
    void(*on_datagram_received)(const uint8_t*, size_t, GSY_StreamId, GSY_PeerId,GSY_ConnectionHwnd);
    void(*on_disconnected)(ErrorCode, GSY_StreamId, GSY_PeerId, const char* reason);
    void(*on_connect_failed)(ErrorCode, GSY_StreamId, GSY_PeerId, GSY_RequestId);
    void(*on_error)(GSY_ConnectionHwnd, GSY_StreamId, ErrorCode, const char*, GSY_RequestId);
    uint32_t self_id;
    const char* verify_token; // hex/binary token from NotifyConnectResult; required for client side
    int verify_token_len;
    int use_relay; // 0 = direct connection, 1 = relay connection; optional for client side, ignored for server side
    int is_server;            // 0 = client (initiator), 1 = server (acceptor)
    void* extend;
    uint8_t _check_code; // do not modify
};

GSY_StreamId GSYNC_EXTERN GSY_ConnectPeerDatagram(GSY_ConnectionHwnd handle,GSY_PeerId,GSY_P2PDatagramConnectionContext*,GSY_RequestId);
ErrorCode GSYNC_EXTERN GSY_SendDatagram(GSY_ConnectionHwnd handle,GSY_StreamId sid,const char* data,size_t size,GSY_RequestId request_id);
ErrorCode GSYNC_EXTERN GSY_ReqDisconnectPeerDatagram(GSY_ConnectionHwnd handle,GSY_StreamId sid,const char* reason,GSY_RequestId request_id);

ErrorCode GSYNC_EXTERN GSY_ServerConnectPeerDatagram(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,GSY_P2PDatagramConnectionContext* context,GSY_RequestId request_id);

#if __cplusplus
}
#endif