#pragma once
#include "lobby.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t GSY_P2PPeerHandle;
#define GSY_INVALID_P2P_PEER ((GSY_P2PPeerHandle)0)

/* Callbacks run on the SDK I/O thread and may run before the initiating
 * function returns. user_data must remain valid until on_closed or on_failed.
 * on_connected reports a successfully authenticated P2P transport.
 * on_stream_open reports both locally and remotely opened streams. */
typedef struct GSYNC_EXTERN GSY_P2PPeerContext {
    void (*on_connected)(GSY_P2PPeerHandle, GSY_ConnectionHwnd, void*);
    void (*on_stream_open)(GSY_P2PPeerHandle, GSY_ConnectionHwnd, GSY_StreamId, void*);
    void (*on_failed)(GSY_P2PPeerHandle, ErrorCode, void*);
    void (*on_closed)(GSY_P2PPeerHandle, ErrorCode, void*);
    void* user_data;
} GSY_P2PPeerContext;

/* Copies config, context and helper data before returning and retains the
 * resolved socket for the peer's lifetime. EC_Pending means accepted;
 * on_failed reports asynchronous failures before on_connected, and on_closed
 * reports termination after on_connected. Exactly one terminal callback is
 * made. Supports IPv4 direct connections only; relay returns EC_NotSupported.
 * A session allows one active attempt and is consumed before on_connected.
 * On failure it can be retried or released using GSY_ReleaseP2PSession.
 * The session expires with its lobby stream. Handshake timeout is ten seconds. */
GSYNC_EXTERN ErrorCode GSY_StartP2PPeer(const char* config_file, GSY_PeerId self_id,
    const GSY_HelperResult* helper, const GSY_P2PPeerContext* context,
    GSY_P2PPeerHandle* peer);
GSYNC_EXTERN ErrorCode GSY_CloseP2PPeer(GSY_P2PPeerHandle peer);

#ifdef __cplusplus
}
#endif
