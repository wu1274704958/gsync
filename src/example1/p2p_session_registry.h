#pragma once
#include <memory>
#include <string>
#include "gsync/core/types.h"
#include "gsync/core/session_handle.h"
namespace mqas::io { class UdpSocket; }
struct P2PSession;
GSY_SessionHandle p2p_session_register(GSY_ConnectionHwnd, GSY_StreamId,
    const std::shared_ptr<mqas::io::UdpSocket>&, std::shared_ptr<const P2PSession>);
ErrorCode p2p_session_acquire(GSY_SessionHandle, std::shared_ptr<mqas::io::UdpSocket>&,
    std::shared_ptr<const P2PSession>&);
void p2p_session_release_attempt(GSY_SessionHandle, const std::shared_ptr<const P2PSession>&);
bool p2p_session_consume(GSY_SessionHandle, const std::shared_ptr<const P2PSession>&);
ErrorCode p2p_session_ready_data(GSY_SessionHandle, GSY_ConnectionHwnd, GSY_StreamId, std::shared_ptr<const P2PSession>&);
bool p2p_session_mark_server_registered(GSY_SessionHandle, const std::shared_ptr<const P2PSession>&);
GSY_SessionHandle p2p_session_mark_ready(GSY_ConnectionHwnd, GSY_StreamId, uint32_t, const std::string&);
bool p2p_session_take_ready(GSY_SessionHandle);
ErrorCode p2p_session_remove(GSY_SessionHandle);
