//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_DEF_H
#define GSYNC_DEF_H

#include <stdint.h>

#if __cplusplus
extern "C" {
#endif

    enum ErrorCode {
        EC_Fail = -1,
        EC_Ok = 0,
        EC_Unknown,
        EC_Pending,
        EC_ErrorBegin = 1000,
        EC_AlreadyInitialized,
        EC_NotInitialized,
        EC_EngineCountLimitExceeded,
        EC_InvalidAddress,
        EC_Disconnected,
        EC_InvalidHandler,
        EC_ConnectFailed,
        EC_ConnectOverLimit,
        EC_EngineNotMatch,
        EC_MakeStreamFailed,
        EC_InvalidStream,
        EC_WrongStreamState,
        //proto::p2p::RetCode begin
        EC_AlreadyExists = 1200,
        EC_NotExists,
        EC_PeerRejected,
        //proto::p2p::RetCode end
    };

    typedef void(*SICallback)(const char*,int);
    typedef void(*ICallback)(int);
    typedef void(*IUICallback)(int,unsigned int);

    typedef unsigned int GSY_ConnectionHwnd;
    typedef unsigned int GSY_EngineId;

    typedef uint64_t GSY_StreamId;


#if __cplusplus
    }
#endif
#endif //GSYNC_DEF_H