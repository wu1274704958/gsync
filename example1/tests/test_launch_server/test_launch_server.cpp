#include <cstdio>
#include <cstring>
#include "../../lobby.h"
#include "core/engine.h"
#include "core/server.h"
#include "../../p2p_connection_datagram.h"

#ifndef CONFIG_PATH
#define CONFIG_PATH "conf.txt"
#endif

static GSY_ServerHwnd g_server_hwnd = InvalidServer;

// ── datagram callbacks (server side) ─────────────────────────────────────────

static void on_datagram_connected(ErrorCode code, GSY_StreamId sid,
                                   GSY_PeerId peer_id, const char* peer_name, GSY_RequestId)
{
    printf("[datagram] on_connected  code=%d  stream=%llu  peer=%u  name=%s\n",
           code, sid, peer_id, peer_name ? peer_name : "");
}

static void on_datagram_received(const uint8_t* data, size_t size,
                                  GSY_StreamId sid, GSY_PeerId,
                                  GSY_ConnectionHwnd chwnd)
{
    printf("[datagram] on_received  conn=%u  stream=%llu  size=%zu  msg=%.*s\n",
           chwnd, sid, size, (int)size, (const char*)data);

    if (size == 5 && memcmp(data, "Hello", 5) == 0)
    {
        printf("[datagram] received 'Hello' -> sending 'Hi'\n");
        GSY_SendDatagram(chwnd, sid, "Hi", 2, NONE_RID);
    }
}

static void on_datagram_disconnected(ErrorCode code, GSY_StreamId sid,
                                      GSY_PeerId peer_id, const char* reason)
{
    printf("[datagram] on_disconnected  code=%d  stream=%llu  peer=%u  reason=%s\n",
           code, sid, peer_id, reason ? reason : "");
}

static void on_datagram_connect_failed(ErrorCode code, GSY_StreamId sid,
                                        GSY_PeerId peer_id, GSY_RequestId)
{
    printf("[datagram] on_connect_failed  code=%d  stream=%llu  peer=%u\n",
           code, sid, peer_id);
}

static void on_datagram_error(GSY_ConnectionHwnd chwnd, GSY_StreamId sid,
                               ErrorCode code, const char* msg, GSY_RequestId)
{
    printf("[datagram] on_error  conn=%u  stream=%llu  code=%d  msg=%s\n",
           chwnd, sid, code, msg ? msg : "");
}

// ── server callbacks ──────────────────────────────────────────────────────────

static void on_started(ErrorCode code, GSY_ServerHwnd hwnd)
{
    printf("[server] on_started  code=%d  hwnd=%u\n", code, hwnd);
}

static void on_stopped(ErrorCode code, GSY_ServerHwnd hwnd)
{
    printf("[server] on_stopped  code=%d  hwnd=%u\n", code, hwnd);
}

static void on_server_error(int code, unsigned int hwnd)
{
    printf("[server] on_error  code=%d  hwnd=%u\n", code, hwnd);
}

static void on_client_disconnect(GSY_ServerHwnd shwnd, GSY_ConnectionHwnd chwnd)
{
    printf("[server] on_client_disconnect  server=%u  conn=%u\n", shwnd, chwnd);
}

// ── per-connection callbacks ──────────────────────────────────────────────────

static void on_conn_connect(int code, unsigned int conn_hwnd)
{
    printf("[conn]   on_connect     code=%d  conn=%u\n", code, conn_hwnd);
}

static void on_conn_disconnect(int code, unsigned int conn_hwnd)
{
    printf("[conn]   on_disconnect  code=%d  conn=%u\n", code, conn_hwnd);
}

static void on_conn_error(int code, unsigned int conn_hwnd)
{
    printf("[conn]   on_error  code=%d  conn=%u\n", code, conn_hwnd);
}

static void on_stream_open(GSY_ConnectionHwnd chwnd, GSY_StreamId sid, ErrorCode code)
{
    printf("[conn]   on_stream_open  conn=%u  stream=%llu  code=%d\n", chwnd, sid, code);

    // register datagram context in server mode for this stream
    auto* dg_ctx                  = new GSY_P2PDatagramConnectionContext{};
    dg_ctx->on_connected          = on_datagram_connected;
    dg_ctx->on_datagram_received  = on_datagram_received;
    dg_ctx->on_disconnected       = on_datagram_disconnected;
    dg_ctx->on_connect_failed     = on_datagram_connect_failed;
    dg_ctx->on_error              = on_datagram_error;
    dg_ctx->is_server             = 1;
    dg_ctx->self_id               = 2;

    const auto ret = GSY_ServerConnectPeerDatagram(chwnd, sid, dg_ctx, NONE_RID);
    printf("[conn]   GSY_ServerConnectPeerDatagram returned code=%d\n", ret);
}

static void on_stream_close(GSY_ConnectionHwnd chwnd, GSY_StreamId sid, ErrorCode code)
{
    printf("[conn]   on_stream_close  conn=%u  stream=%llu  code=%d\n", chwnd, sid, code);
}

// ── on_client_connect: allocate per-connection context ───────────────────────

static GSY_BaseConnectionContext* on_client_connect(GSY_ServerHwnd shwnd, GSY_ConnectionHwnd chwnd)
{
    printf("[server] on_client_connect  server=%u  conn=%u\n", shwnd, chwnd);
    auto* ctx            = new GSY_BaseConnectionContext{};
    ctx->on_connect      = on_conn_connect;
    ctx->on_disconnect   = on_conn_disconnect;
    ctx->on_error        = on_conn_error;
    ctx->on_stream_open  = on_stream_open;
    ctx->on_stream_close = on_stream_close;
    return ctx;
}

// ── global engine error ───────────────────────────────────────────────────────

static void on_global_error(const char* msg, int code)
{
    printf("[global] on_error  code=%d  msg=%s\n", code, msg);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    GSY_Context cxt{ .on_error = on_global_error };
    GSY_initialize(0, &cxt);

    GSY_BaseServerContext server_ctx{};
    server_ctx.on_started           = on_started;
    server_ctx.on_stopped           = on_stopped;
    server_ctx.on_error             = on_server_error;
    server_ctx.on_client_connect    = on_client_connect;
    server_ctx.on_client_disconnect = on_client_disconnect;

    // engine_id=2 → P2PDatagramEngine (second entry in AllEngineType)
    g_server_hwnd = GSY_launch_server(2, CONFIG_PATH, &server_ctx);
    printf("[main] GSY_launch_server returned hwnd=%u\n", g_server_hwnd);

    printf("[main] Press Enter to exit...\n");
    getchar();

    if (g_server_hwnd != InvalidServer)
    {
        printf("[main] Stopping server hwnd=%u\n", g_server_hwnd);
        GSY_stop_server(g_server_hwnd);
        g_server_hwnd = InvalidServer;
    }

    GSY_terminate();
    printf("[main] Done.\n");
    return 0;
}
