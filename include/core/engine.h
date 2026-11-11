//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_CORE_H
#define GSYNC_CORE_H

#include "macro.h"
#include "def.h"

#if __cplusplus
extern "C" {
#endif

    typedef struct GSYNC_EXTERN GSY_Context {
        SICallback on_error;
    } GSY_Context;

    int GSYNC_EXTERN GSY_initialize(int flag,GSY_Context* cxt);
    // Must not be called from an SDK I/O callback; returns EC_Fail there.
    int GSYNC_EXTERN GSY_terminate();

#if __cplusplus
}
#endif

#endif //GSYNC_CORE_H
