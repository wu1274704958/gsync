//
// Created by shuaijiewu on 5/20/2026.
//

#ifndef GSYNC_SERVER_H
#define GSYNC_SERVER_H

#include "macro.h"
#include "def.h"
#include "connection.h"

#if __cplusplus
extern "C" {
#endif

    typedef unsigned int GSY_ServerHwnd;
    inline GSY_ServerHwnd InvalidServer = 0;

    struct GSYNC_EXTERN GSY_BaseServerContext {
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
    };

    // 启动服务器，返回 GSY_ServerHwnd；engine_id 用于匹配 AllEngineType 中对应的 EngineType
    GSY_ServerHwnd GSYNC_EXTERN GSY_launch_server(GSY_EngineId engine_id,
                                                   const char* config_file,
                                                   GSY_BaseServerContext* cxt);

    // 停止服务器（异步，内部通过 push_task 执行 close）

    int GSYNC_EXTERN GSY_stop_server(GSY_ServerHwnd hwnd);

#if __cplusplus
}
#endif

#endif //GSYNC_SERVER_H
