//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_DEF_H
#define GSYNC_DEF_H

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

#if __cplusplus
    }
#endif
#endif //GSYNC_DEF_H