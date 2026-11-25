//
// Created by shuaijiewu on 1/6/2026.
//

#ifndef GSYNC_CONNECTION_H
#define GSYNC_CONNECTION_H

#include "gsync/core/macro.h"
#include "gsync/core/types.h"
#include "session_handle.h"

#if __cplusplus
extern "C" {
#endif

    #ifdef __cplusplus
    inline GSY_ConnectionHwnd InvalidConnection = 0;
    #else
    #define InvalidConnection ((GSY_ConnectionHwnd)0)
    #endif

    typedef uint64_t GSY_SocketHandle;
    #define GSY_INVALID_SOCKET ((GSY_SocketHandle)0)

    typedef struct GSY_ContextExtension {
        uint32_t type;
        uint32_t size;
    } GSY_ContextExtension;

    typedef struct GSYNC_EXTERN GSY_BaseConnectionContext {
        IUICallback on_connect;
        IUICallback on_disconnect;
        IUICallback on_error;
        void(*on_stream_open)(GSY_ConnectionHwnd,GSY_StreamId,ErrorCode);
        void(*on_stream_close)(GSY_ConnectionHwnd,GSY_StreamId,ErrorCode);
        void* extend;
        /* Read synchronously by GSY_connect. NULL selects the configured endpoint. */
        const GSY_ContextExtension* options;
    } GSY_BaseConnectionContext;

    /* Fixed bind endpoints reuse a matching engine when its normalized config
       matches; config port 0 always creates a new engine. Asynchronous failures
       are delivered to cxt->on_error. The context must outlive its callbacks. */
    GSY_ConnectionHwnd GSYNC_EXTERN GSY_connect(GSY_EngineId engine_id,const char* config_file,
        const char* ip,short port,GSY_BaseConnectionContext* cxt);
    int GSYNC_EXTERN GSY_disconnect(GSY_ConnectionHwnd handler);
    int GSYNC_EXTERN GSY_is_connected(GSY_ConnectionHwnd handler);

    /* The SDK handle is valid while the UDP socket has a live owner.
       GSY_ReleaseSocket explicitly unregisters the handle. */
    ErrorCode GSYNC_EXTERN GSY_GetConnectionSocket(GSY_ConnectionHwnd, GSY_SocketHandle*);
    ErrorCode GSYNC_EXTERN GSY_ReleaseSocket(GSY_SocketHandle);
    /* Compares the underlying live sockets, not the opaque handle values. */
    ErrorCode GSYNC_EXTERN GSY_AreSameSocket(GSY_SocketHandle, GSY_SocketHandle, int* same);
    /* Returns EC_Pending with a reserved connection handle on success. */
    ErrorCode GSYNC_EXTERN GSY_connect_session(const char* config_file, GSY_SessionHandle,
        GSY_BaseConnectionContext*, GSY_ConnectionHwnd*);

#if __cplusplus
}
#endif
#endif //GSYNC_CONNECTION_H
