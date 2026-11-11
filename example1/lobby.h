//
// Created by shuaijiewu on 10/10/2025.
//

#ifndef GSYNC_EXAMPLE1_H
#define GSYNC_EXAMPLE1_H

#ifdef __cplusplus
#include <cstring>
#else
#include <string.h>
#include <stdbool.h>
#endif

#include "core/engine.h"
#include "core/connection.h"
#include "core/session_handle.h"

#if __cplusplus
extern "C" {
#endif

    typedef uint32_t GSY_PeerId;
    typedef uint32_t GSY_RequestId;

    #ifdef __cplusplus
    const GSY_StreamId INVALID_SID = 0;
    const GSY_PeerId INVALID_PID = 0;
    const GSY_RequestId NONE_RID = 0;
    #else
    #define INVALID_SID ((GSY_StreamId)0)
    #define INVALID_PID ((GSY_PeerId)0)
    #define NONE_RID ((GSY_RequestId)0)
    #endif

    typedef struct GSYNC_EXTERN GSY_PeerData
    {
        GSY_PeerId peer_id;
        const char* name;
    } GSY_PeerData;

    typedef struct GSYNC_EXTERN GSY_sockaddr {
        char ip[40];
        uint16_t port;
    } GSY_sockaddr;

    static inline void init_sockaddr_data(GSY_sockaddr* addr)
    {
        memset(addr, 0, sizeof(GSY_sockaddr));
    }

    typedef struct GSYNC_EXTERN GSY_HelperResult
    {
        ErrorCode ret;
        uint32_t peer_id;
        int is_server;
        GSY_sockaddr peer_addr;
        const char* reason;
        GSY_sockaddr address;
        GSY_sockaddr relay_addr;
        int use_relay;
        const char* relay_token;
        /* One helper negotiation; expires with the lobby stream and is consumed
           when its P2P connection authenticates. */
        GSY_SessionHandle session_handle;
        // Reserved for ABI compatibility; authenticated P2P keeps credentials inside the SDK.
        const uint8_t* verify_token;
        size_t verify_token_len;
    } GSY_HelperResult;

    typedef struct GSYNC_EXTERN GSY_LobbyStreamContext {
        void(*on_registration_success)(ErrorCode,GSY_StreamId,GSY_PeerId);
        void(*on_unregister)(ErrorCode,GSY_StreamId);
        void(*on_connect_responds)(ErrorCode,/*other peer*/GSY_PeerId,GSY_StreamId);
        void(*on_receive_peer_list)(GSY_PeerData*,/*size of list*/uint32_t,GSY_StreamId);
        void(*on_peer_req_connect)(GSY_PeerData*,GSY_StreamId);
        void(*on_error)(GSY_ConnectionHwnd,GSY_StreamId,ErrorCode,const char*,GSY_RequestId);
        void(*on_change_to_helper_result)(GSY_StreamId,ErrorCode,GSY_PeerId);
        void(*on_attempt_connect)(GSY_StreamId,/*ip*/const char*,/*port*/uint16_t,/*times*/uint32_t,/*verify_code*/uint32_t);
        void(*on_helper_quit_result)(GSY_StreamId,GSY_HelperResult*);
        void* extend;
        uint8_t _check_code;//do not modify
    } GSY_LobbyStreamContext;

    GSY_StreamId GSYNC_EXTERN GSY_RegisterToLobby(GSY_ConnectionHwnd handle,const char* name,const char* psd,GSY_LobbyStreamContext*);
    ErrorCode GSYNC_EXTERN GSY_UnregisterFromLobby(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_FetchPeerList(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_RequestConnectPeer(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_PeerId peer_id,GSY_RequestId request_id);
    ErrorCode GSYNC_EXTERN GSY_RespondPeerConnectRequest(GSY_ConnectionHwnd handle,GSY_StreamId sid,GSY_PeerId peer_id,bool accept,GSY_RequestId request_id);
    /* Release an unused session after failure or when it is no longer needed.
       A session with an active connection attempt returns EC_SessionBusy;
       close the peer and wait for on_failed before releasing it. */
    ErrorCode GSYNC_EXTERN GSY_ReleaseP2PSession(GSY_SessionHandle session);

#if __cplusplus
}
#endif
#endif
