//
// Created by shuaijiewu on 10/10/2025.
//

#ifndef GSYNC_EXAMPLE1_H
#define GSYNC_EXAMPLE1_H

#include "macro.h"
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
    };

    struct PeerData {
        unsigned int id = 1;
        const char* name;
    };

    typedef void(*SICallback)(const char*,int);
    typedef void(*ICallback)(int);
    typedef void(*IUICallback)(int,unsigned int);
    typedef void(*PDCallback)(struct PeerData*);

    int GSYNC_EXTERN GSY_initialize(int flag,SICallback error_callback);
    int GSYNC_EXTERN GSY_terminate();

    unsigned int GSYNC_EXTERN GSY_connect_hole_punching_server(const char* config_file,const char* name,const char* psd,IUICallback callback,PDCallback req_connect_cb);
    int GSYNC_EXTERN GSY_disconnect_hole_punching_server(unsigned int handler);
    int GSYNC_EXTERN GSY_is_connected_hole_punching_server(unsigned int handler);
#if __cplusplus
}
#endif
#endif