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
        EC_ConnectFailed,
        EC_ConnectOverLimit,
        EC_EngineNotMatch
    };

    typedef void(*SICallback)(const char*,int);
    typedef void(*ICallback)(int);
    typedef void(*IUICallback)(int,unsigned int);

    typedef unsigned int GSY_ConnectionHwnd;
    typedef unsigned int GSY_EngineId;

    inline GSY_ConnectionHwnd InvalidConnection = 0;

    struct GSYNC_EXTERN GSY_Context {
        SICallback on_error;
    };

    struct GSYNC_EXTERN GSY_BaseConnectionContext {
        IUICallback on_connect;
        IUICallback on_disconnect;
        IUICallback on_error;
        void* extend;
    };

    int GSYNC_EXTERN GSY_initialize(int flag,GSY_Context* cxt);
    int GSYNC_EXTERN GSY_terminate();

    GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,
        const char* ip,short port,GSY_BaseConnectionContext* cxt);
    int GSYNC_EXTERN GSY_disconnect(GSY_ConnectionHwnd handler);
    int GSYNC_EXTERN GSY_is_connected(GSY_ConnectionHwnd handler);

#if __cplusplus
}
#endif
#endif