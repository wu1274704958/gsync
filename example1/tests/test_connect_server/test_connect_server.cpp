#include <cstdio>
#include <cstring>
#include <mqas/comm/binary.hpp>
#include <mqas/comm/locator.h>
#include <mqas/tools/stream/MsgDef.h>

#include "../../lobby.h"
#include "core/engine.h"
#include "core/connection.h"
#include "../../p2p_connection_datagram.h"

#ifndef CONFIG_PATH
#define CONFIG_PATH "conf.txt"
#endif

static GSY_ConnectionHwnd g_conn_hwnd = InvalidConnection;
static GSY_StreamId       g_stream_id = INVALID_SID;
static const char* Token = "12345678";

// ── datagram callbacks (client side) ─────────────────────────────────────────

static void on_datagram_connected(ErrorCode code, GSY_StreamId sid,
                                   GSY_PeerId peer_id, const char* peer_name, GSY_RequestId)
{
    printf("[datagram] on_connected  code=%d  stream=%llu  peer=%u  name=%s\n",
           code, sid, peer_id, peer_name ? peer_name : "");

    if (code == EC_Ok)
    {
        g_stream_id = sid;
        printf("[datagram] connected -> sending 'Hello'\n");
        GSY_SendDatagram(g_conn_hwnd, sid, "Hello", 5, NONE_RID);
    }
}

static void on_datagram_received(const uint8_t* data, size_t size,
                                  GSY_StreamId sid, GSY_PeerId,
                                  GSY_ConnectionHwnd chwnd)
{
    printf("[datagram] on_received  conn=%u  stream=%llu  size=%zu  msg=%.*s\n",
           chwnd, sid, size, (int)size, (const char*)data);
}

static void on_datagram_disconnected(ErrorCode code, GSY_StreamId sid,
                                      GSY_PeerId peer_id, const char* reason)
{
    printf("[datagram] on_disconnected  code=%d  stream=%llu  peer=%u  reason=%s\n",
           code, sid, peer_id, reason ? reason : "");
    g_stream_id = INVALID_SID;
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

// ── connection callbacks ──────────────────────────────────────────────────────

static void on_connect(int code, unsigned int conn_hwnd)
{
    printf("[conn] on_connect  code=%d  conn=%u\n", code, conn_hwnd);

    if (code == EC_Ok)
    {
        g_conn_hwnd = conn_hwnd;
        // open a datagram stream as client (is_server=0)
        auto* dg_ctx                  = new GSY_P2PDatagramConnectionContext{};
        dg_ctx->on_connected          = on_datagram_connected;
        dg_ctx->on_datagram_received  = on_datagram_received;
        dg_ctx->on_disconnected       = on_datagram_disconnected;
        dg_ctx->on_connect_failed     = on_datagram_connect_failed;
        dg_ctx->on_error              = on_datagram_error;
        dg_ctx->is_server             = 0;
        dg_ctx->verify_token          = Token;
        dg_ctx->verify_token_len      = static_cast<int>(strlen(Token));
        dg_ctx->self_id = 1;
        GSY_ConnectPeerDatagram(conn_hwnd, INVALID_PID, dg_ctx, NONE_RID);
    }
}

static void on_disconnect(int code, unsigned int conn_hwnd)
{
    printf("[conn] on_disconnect  code=%d  conn=%u\n", code, conn_hwnd);
    g_conn_hwnd = InvalidConnection;
    g_stream_id = INVALID_SID;
}

static void on_conn_error(int code, unsigned int conn_hwnd)
{
    printf("[conn] on_error  code=%d  conn=%u\n", code, conn_hwnd);
}

static void on_stream_open(GSY_ConnectionHwnd chwnd, GSY_StreamId sid, ErrorCode code)
{
    printf("[conn] on_stream_open  conn=%u  stream=%llu  code=%d\n", chwnd, sid, code);
}

static void on_stream_close(GSY_ConnectionHwnd chwnd, GSY_StreamId sid, ErrorCode code)
{
    printf("[conn] on_stream_close  conn=%u  stream=%llu  code=%d\n", chwnd, sid, code);
}

// ── global error ──────────────────────────────────────────────────────────────

static void on_global_error(const char* msg, int code)
{
    printf("[global] on_error  code=%d  msg=%s\n", code, msg);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    GSY_Context cxt{ .on_error = on_global_error };
    GSY_initialize(0, &cxt);

    GSY_BaseConnectionContext conn_ctx{};
    conn_ctx.on_connect      = on_connect;
    conn_ctx.on_disconnect   = on_disconnect;
    conn_ctx.on_error        = on_conn_error;
    conn_ctx.on_stream_open  = on_stream_open;
    conn_ctx.on_stream_close = on_stream_close;

    // engine_id=2 → P2PDatagramEngine; connects to test_launch_server on 127.0.0.1:8101
    g_conn_hwnd = GSY_connect(2, CONFIG_PATH, "127.0.0.1", 8101, &conn_ctx);
    printf("[main] GSY_connect returned conn=%u\n", g_conn_hwnd);

    printf("[main] Press Enter to exit...\n");
    getchar();

    if (g_conn_hwnd != InvalidConnection)
    {
        printf("[main] Disconnecting conn=%u\n", g_conn_hwnd);
        GSY_disconnect(g_conn_hwnd);
        g_conn_hwnd = InvalidConnection;
    }

    GSY_terminate();
    printf("[main] Done.\n");
    return 0;
}
