#include "../p2p_peer.h"
#include "../p2p_connection_datagram.h"

static void connected(GSY_P2PPeerHandle peer, GSY_ConnectionHwnd connection, void* user)
{
    (void)peer; (void)connection; (void)user;
}

int main(void)
{
    GSY_P2PPeerContext peer = {0};
    GSY_HelperResult helper = {0};
    GSY_P2PDatagramConnectionContext datagram = {0};
    peer.on_connected = connected;
    GSY_P2PPeerHandle handle = 42;
    if (!peer.on_connected || helper.session_handle != GSY_INVALID_SESSION_HANDLE || datagram.self_id != 0)
        return 1;
    if (GSY_StartP2PPeer(0, 0, 0, 0, &handle) != EC_NotInitialized || handle != GSY_INVALID_P2P_PEER)
        return 2;
    if (GSY_ReleaseP2PSession(GSY_INVALID_SESSION_HANDLE) != EC_NotInitialized) return 3;
    return 0;
}
