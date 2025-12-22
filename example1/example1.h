//
// Created by shuaijiewu on 10/10/2025.
//

#ifndef GSYNC_EXAMPLE1_H
#define GSYNC_EXAMPLE1_H

#include <memory>

#include "macro.h"


namespace mqas::tools::proto::p2p {
    class PeerData;
    class RespondConnectPeer;
}

#if __cplusplus
extern "C" {
#endif

    enum ErrorCode {
        EC_Ok = 0,

        EC_ErrorBegin = 1000,
        EC_AlreadyInitialized,
        EC_NotInitialized,
        EC_EngineCountLimitExceeded,
        EC_InvalidAddress,
        EC_Disconnected,
        EC_InvalidHandler,
        EC_ConnectFailed,
    };

    typedef void(*SICallback)(const char*,int);
    typedef void(*ICallback)(int);
    typedef void(*IUICallback)(int,unsigned int);

    struct GSYNC_EXTERN GSY_Context {
        SICallback on_error;
    };

    struct GSYNC_EXTERN GSY_HPConnectContext {
        IUICallback on_connect;
        IUICallback on_disconnect;
        IUICallback on_error;
        void(*on_request_connect)(const mqas::tools::proto::p2p::PeerData& peer);
        void(*on_response_connect)(std::shared_ptr<mqas::tools::proto::p2p::RespondConnectPeer> respond);
    };

    int GSYNC_EXTERN GSY_initialize(int flag,GSY_Context context);
    int GSYNC_EXTERN GSY_terminate();

    unsigned int GSYNC_EXTERN GSY_connect_hole_punching_server(const char* config_file,const char* name,const char* psd,GSY_HPConnectContext context);
    int GSYNC_EXTERN GSY_disconnect_hole_punching_server(unsigned int handler);
    int GSYNC_EXTERN GSY_is_connected_hole_punching_server(unsigned int handler);
#if __cplusplus
}
#endif
#endif