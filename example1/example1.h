//
// Created by shuaijiewu on 10/10/2025.
//

#ifndef GSYNC_EXAMPLE1_H
#define GSYNC_EXAMPLE1_H

#include "core/engine.h"
#include "core/connection.h"
#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

    typedef uint64_t GSY_StreamId;
    typedef uint32_t GSY_PeerId;
    typedef uint32_t GSY_RequestId;

    const GSY_StreamId INVALID_SID = 0;
    const GSY_PeerId INVALID_PID = 0;
    const GSY_RequestId NONE_RID = 0;

    struct GSYNC_EXTERN GSY_PeerData
    {
        GSY_PeerId peer_id;
        const char* name;
    };

    struct GSYNC_EXTERN GSY_LobbyStreamContext {
        void(*on_registration_success)(ErrorCode,GSY_StreamId,GSY_PeerId);
        void(*on_unregister)(ErrorCode,GSY_StreamId);
        void(*on_connect_responds)(ErrorCode,/*other peer*/GSY_PeerId,GSY_StreamId);
        void(*on_receive_peer_list)(GSY_PeerData*,/*size of list*/uint32_t,GSY_StreamId);
        void(*on_peer_req_connect)(GSY_PeerData*,GSY_StreamId);
        void(*on_error)(GSY_ConnectionHwnd,GSY_StreamId,ErrorCode,const char*,GSY_RequestId);
        void* extend;
        uint8_t _check_code;//do not modify
    };

    GSY_StreamId GSYNC_EXTERN GSY_RegisterToLobby(GSY_ConnectionHwnd handle,const char* name,const char* psd,GSY_LobbyStreamContext*);
    ErrorCode GSYNC_EXTERN GSY_UnregisterFromLobby(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_FetchPeerList(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_RequestConnectPeer(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_PeerId peer_id,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_RespondPeerConnectRequest(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_PeerId peer_id,bool accept,GSY_RequestId request_id);

#if __cplusplus
}
#endif
#endif