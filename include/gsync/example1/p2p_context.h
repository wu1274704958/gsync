#pragma once

#include "gsync/core/connection.h"
#include "gsync/core/server.h"
#include "gsync/example1/types.h"

#ifdef __cplusplus
extern "C" {
#endif

    enum { GSY_CONTEXT_P2P_CONNECT = 1, GSY_CONTEXT_P2P_SERVER = 2, GSY_CONTEXT_P2P_SESSION_SERVER = 3 };

    typedef struct GSY_P2PConnectOptions {
        GSY_ContextExtension header;
        GSY_SessionHandle session;
    } GSY_P2PConnectOptions;

    typedef int (*GSY_P2PStreamPolicy)(GSY_ServerHwnd, GSY_ConnectionHwnd, GSY_StreamId,
        GSY_P2PStreamId, GSY_P2PStreamId*, const char**);

    typedef struct GSY_P2PServerOptions {
        GSY_ContextExtension header;
        GSY_SocketHandle socket;
        GSY_P2PStreamPolicy stream_policy;
    } GSY_P2PServerOptions;

    /* Launch on the negotiated socket. Register the session after server startup. */
    typedef struct GSY_P2PSessionServerOptions {
        GSY_ContextExtension header;
        GSY_SessionHandle session;
        GSY_P2PStreamPolicy stream_policy;
    } GSY_P2PSessionServerOptions;

    /* Available after the P2P connection has been authenticated. */
    ErrorCode GSYNC_EXTERN GSY_GetConnectionP2PPeerId(GSY_ConnectionHwnd, GSY_PeerId*);

    ErrorCode GSYNC_EXTERN GSY_RegisterServerP2PSession(GSY_ServerHwnd, GSY_SessionHandle);
    ErrorCode GSYNC_EXTERN GSY_CancelServerP2PSession(GSY_ServerHwnd, GSY_SessionHandle);

#ifdef __cplusplus
}
#endif
