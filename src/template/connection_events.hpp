#pragma once

#include "../common.h"

// The transport must be registered before delivering its stream callbacks.
struct AllowBaseStreams { bool operator()() const { return true; } };

template<class Connection, class Allow = AllowBaseStreams>
void setup_base_stream_events(Connection& connection, GSY_ConnectionHwnd handle,
    GSY_BaseConnectionContext* context, Allow allow = {})
{
    connection.set_cxt(context);
    connection.on_new_stream_signal.connect([handle, context, allow](const auto& stream) {
        if (context && context->on_stream_open && allow())
            context->on_stream_open(handle, reinterpret_cast<GSY_StreamId>(stream->get_origin()), EC_Ok);
    });
    connection.on_stream_close_signal.connect([handle, context](const auto& stream) {
        const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
        p2p_session_remove_stream(handle, sid);
        if (context && context->on_stream_close) context->on_stream_close(handle, sid, EC_Ok);
    });
}
