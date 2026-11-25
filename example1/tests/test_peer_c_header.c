#include "../p2p_peer.h"
#include "../p2p_connection_datagram.h"
#include "core/p2p_context.h"

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
    GSY_SocketHandle socket = 42;
    if (GSY_GetConnectionSocket(0, &socket) != EC_NotInitialized || socket != GSY_INVALID_SOCKET) return 4;
    if (GSY_ReleaseSocket(GSY_INVALID_SOCKET) != EC_NotInitialized) return 5;
    GSY_BaseServerContext server = {0};
    if (server.options != 0 ||
        GSY_RegisterServerP2PSession(InvalidServer, GSY_INVALID_SESSION_HANDLE) != EC_NotInitialized) return 6;
    GSY_ConnectionHwnd connection = 42;
    if (GSY_connect_session(0, GSY_INVALID_SESSION_HANDLE, 0, &connection) != EC_NotInitialized ||
        connection != InvalidConnection) return 7;
    return 0;
}
