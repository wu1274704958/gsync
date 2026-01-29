//
// Created by shuaijiewu on 10/10/2025.
//

#ifndef GSYNC_EXAMPLE1_H
#define GSYNC_EXAMPLE1_H

#include <cstring>

#include "core/engine.h"
#include "core/connection.h"

#if __cplusplus
extern "C" {
#endif

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

    struct GSYNC_EXTERN GSY_sockaddr {
        char ip[40];
        uint16_t port;
    };

    inline void init_sockaddr_data(GSY_sockaddr* addr)
    {
        memset(addr, 0, sizeof(GSY_sockaddr));
    }

    struct GSYNC_EXTERN GSY_HelperResult
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
        size_t socket_handle;
    };

    struct GSYNC_EXTERN GSY_LobbyStreamContext {
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