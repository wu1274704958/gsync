//
// Created by shuaijiewu on 5/20/2026.
//

#ifndef GSYNC_SERVER_H
#define GSYNC_SERVER_H

#include "gsync/core/macro.h"
#include "gsync/core/types.h"
#include "connection.h"
#include "session_handle.h"

#if __cplusplus
extern "C" {
#endif

    typedef unsigned int GSY_ServerHwnd;
#define InvalidServer ((GSY_ServerHwnd)0)
    typedef struct GSYNC_EXTERN GSY_BaseServerContext {
        // 服务器启动成功/失败回调
        void(*on_started)(ErrorCode, GSY_ServerHwnd);
        // 服务器停止完成回调
        void(*on_stopped)(ErrorCode, GSY_ServerHwnd);
        // 服务器级别错误 (ErrorCode, GSY_ServerHwnd)
        IUICallback on_error;
        // 新客户端连入：业务层分配并返回该连接专属 Context；返回 nullptr 则拒绝该连接
        GSY_BaseConnectionContext*(*on_client_connect)(GSY_ServerHwnd, GSY_ConnectionHwnd);
        // 客户端断开
        void(*on_client_disconnect)(GSY_ServerHwnd, GSY_ConnectionHwnd);
        void* extend;
        /* Read synchronously by GSY_launch_server. */
        const GSY_ContextExtension* options;
    } GSY_BaseServerContext;

    // 兼容的固定监听地址可共享 engine；各句柄按启动顺序尝试接收新连接。
    // cxt 须保持有效，直到启动失败回调或停止回调返回。
    GSY_ServerHwnd GSYNC_EXTERN GSY_launch_server(GSY_EngineId engine_id,
                                                   const char* config_file,
                                                   GSY_BaseServerContext* cxt);

    // 异步停止此句柄并关闭它接受的连接；连接断开回调完成后触发 on_stopped。

    int GSYNC_EXTERN GSY_stop_server(GSY_ServerHwnd hwnd);

#if __cplusplus
}
#endif

#endif //GSYNC_SERVER_H
