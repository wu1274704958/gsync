//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_CONNECTION_H
#define GSYNC_CONNECTION_H

#include "macro.h"
#include "def.h"

#if __cplusplus
extern "C" {
#endif

    inline GSY_ConnectionHwnd InvalidConnection = 0;

    struct GSYNC_EXTERN GSY_BaseConnectionContext {
        IUICallback on_connect;
        IUICallback on_disconnect;
        IUICallback on_error;
        void* extend;
    };

    GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,
        const char* ip,short port,GSY_BaseConnectionContext* cxt);
    int GSYNC_EXTERN GSY_disconnect(GSY_ConnectionHwnd handler);
    int GSYNC_EXTERN GSY_is_connected(GSY_ConnectionHwnd handler);

#if __cplusplus
}
#endif
#endif //GSYNC_CONNECTION_H