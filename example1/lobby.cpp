#include "lobby.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/io/ip.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../src/common.h"
#include "lib.hpp"
#include "p2p_session_registry.h"
#include "p2p_session.h"
#include <openssl/crypto.h>


constexpr uint8_t ContextCheckCode = 192;

ErrorCode GSY_ReleaseP2PSession(GSY_SessionHandle session) {
    if (!is_running) return EC_NotInitialized;
    return p2p_session_remove(session);
}

void init_helper_stream(std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream, GSY_LobbyStreamContext* context, GSY_StreamId stream_id, GSY_ConnectionHwnd owner, std::shared_ptr<GSY_PeerId> self_id);

GSY_StreamId GSY_RegisterToLobby(GSY_ConnectionHwnd handle,const char* name,const char* psd,GSY_LobbyStreamContext* context)
{
    if (!context) return INVALID_SID;
    context->_check_code = ContextCheckCode;
    std::shared_ptr<mqas::core::IConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connect_mutex);
        if (!connect_map.contains(handle) || connect_map.at(handle).expired())
        {
            if (context->on_error)
                context->on_error(handle,INVALID_SID,EC_InvalidHandler,"Invalid connection handle",NONE_RID);
            return INVALID_PID;
        }
        conn = connect_map.at(handle).lock();
        if (!conn)
        {
            if (context->on_error)
                context->on_error(handle,INVALID_SID,EC_InvalidHandler,"Connect maybe disconnected",NONE_RID);
            return INVALID_PID;
        }
    }
    // This synchronous API cannot wait on the SDK I/O thread.
    if (std::this_thread::get_id() == main_thread->get_id()) {
        if (context->on_error)
            context->on_error(handle, INVALID_SID, EC_Fail, "RegisterToLobby cannot block the I/O thread", NONE_RID);
        return INVALID_SID;
    }
    auto completion = std::make_shared<std::promise<GSY_StreamId>>();
    auto result = completion->get_future();
    push_task([conn, context, name_copy = std::string(name ? name : ""), handle, completion]()
    {
        const auto connect = std::static_pointer_cast<mqas::core::Connect<HolePunchingStream>>(conn);
        const auto setup = [context, name_copy, handle](std::shared_ptr<HolePunchingStream> stream) -> GSY_StreamId {
        if (stream == nullptr)
        {
            if (context->on_error)
                context->on_error(handle,INVALID_SID,EC_MakeStreamFailed,"Make stream failed",NONE_RID);
            return INVALID_PID;
        }
        mqas::tools::proto::p2p::ReqRegistePeer msg;
        msg.set_name(name_copy);
        if(!stream->req_change<mqas::tools::p2p::P2PLobbyClientStream, mqas::tools::p2p::ReqRegistePeerPair>(msg))
        {
            if(context->on_error)
              context->on_error(handle,INVALID_SID,EC_MakeStreamFailed,"Send request failed",NONE_RID);
            stream->close();
            return INVALID_PID;
        }
        const auto stream_id = reinterpret_cast<GSY_StreamId>(stream->get_origin());

        stream->on_quit_stream_signal.connect([context, stream_id](std::shared_ptr<mqas::core::IStreamVariant> stream)
        {
            if (stream->getStreamTag() == P2PLobbyStreamIndex)
            {
                if (context->on_unregister)
                {
                    context->on_unregister(EC_Ok,stream_id);
                }
            }
        });

        auto lobby_stream = stream->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();
        auto self_id = std::make_shared<GSY_PeerId>(0);
        auto init_helper_stream_func = [context,stream_id,handle,self_id](std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream)
        {
            init_helper_stream(std::move(helper_stream), context, stream_id, handle, self_id);
        };
        lobby_stream->on_change_helper_by_req = [stream, init_helper_stream_func](const mqas::tools::proto::p2p::ReqConnectPeer& msg)
        {
            stream->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqConnectPeerPair>(msg);
            init_helper_stream_func(stream->get_holds_stream<mqas::tools::p2p::P2PHelperClientStream>());
        };
        lobby_stream->on_change_helper = [stream, init_helper_stream_func](const mqas::tools::proto::p2p::ReqRespondPeerReqConnect& msg)
        {
            stream->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqRespondPeerReqConnectPair>(msg);
            init_helper_stream_func(stream->get_holds_stream<mqas::tools::p2p::P2PHelperClientStream>());
        };
        lobby_stream->on_register_signal.connect([context,stream_id,self_id](const std::shared_ptr<mqas::tools::proto::p2p::RespondRegistePeer>& msg)
        {
            if (msg->ret() == mqas::tools::proto::p2p::RetCode::ok) *self_id = msg->id();
            if (context->on_registration_success)
                context->on_registration_success(mapping_ret_code(msg->ret()), stream_id, msg->id());
        });
        lobby_stream->on_request_connect_signal.connect([context, stream_id](const mqas::tools::proto::p2p::PeerData& peer)
        {
            if (context->on_peer_req_connect)
            {
                GSY_PeerData peer_data={};
                peer_data.name = peer.name().c_str();
                peer_data.peer_id = peer.id();
                context->on_peer_req_connect(&peer_data,stream_id);
            }
        });
        lobby_stream->on_peer_list_signal.connect([context, stream_id](std::shared_ptr<mqas::tools::proto::p2p::RespondPeerList> list)
        {
            static std::vector<GSY_PeerData> peer_list;
            if (context->on_receive_peer_list)
            {
                if (peer_list.size() < list->peer_list_size())
                    peer_list.resize(list->peer_list_size());
                for (int i = 0; i < list->peer_list_size(); i++)
                {
                    peer_list[i].name = list->peer_list().at(i).name().c_str();
                    peer_list[i].peer_id = list->peer_list().at(i).id();
                }
                context->on_receive_peer_list(peer_list.data(), list->peer_list_size(),stream_id);
            }
        });
        lobby_stream->on_connect_response_signal.connect([context, stream_id](std::shared_ptr<mqas::tools::proto::p2p::RespondConnectPeer> msg)
        {
            if (context->on_connect_responds)
            {
                context->on_connect_responds(mapping_ret_code(msg->ret()),msg->peer_id(),stream_id);
            }
        });
        return stream_id;
        };
        if (const auto it = connect->enumerate_stream(); it != connect->enumerate_stream_end()) {
            const auto stream = it->second;
            if (stream->get_current_stream_tag() != 0) {
                const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
                if (context->on_error)
                    context->on_error(handle, sid, EC_WrongStreamState, "Already registered", NONE_RID);
                completion->set_value(sid);
            } else {
                try { completion->set_value(setup(stream)); }
                catch (...) { completion->set_value(INVALID_SID); }
            }
            return;
        }
        const auto rid = connect->make_stream([setup, completion, context](uint64_t,
            std::shared_ptr<HolePunchingStream> stream) {
            if (stream) stream->set_cxt(context);
            try { completion->set_value(setup(std::move(stream))); }
            catch (...) { completion->set_value(INVALID_SID); }
        });
        if (!rid) completion->set_value(INVALID_SID);
    });
    return result.get();
}

ErrorCode GSY_UnregisterFromLobby(GSY_ConnectionHwnd handle, GSY_StreamId sid,GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    push_task([conn,request_id, sid, handle]()
    {
        const auto stream = check_stream_valid<P2PLobbyStreamPair,
            HolePunchingStream,GSY_LobbyStreamContext,ContextCheckCode>(handle,conn,sid,request_id);
        if (stream)
        {
            const mqas::tools::proto::p2p::ReqUnregistePeer msg;
            const auto s = stream->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();
            s->send_req_quit<mqas::tools::p2p::ReqUnregistePeerPair>(msg);
        }
    });

    return EC_Pending;
}

ErrorCode GSY_FetchPeerList(GSY_ConnectionHwnd handle, GSY_StreamId stream_id, GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    push_task([conn,request_id, stream_id, handle]()
    {
        const auto stream = check_stream_valid<P2PLobbyStreamPair,
            HolePunchingStream,GSY_LobbyStreamContext,ContextCheckCode>(handle,conn,stream_id,request_id);
        if (stream)
        {
            const auto s = stream->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();
            s->req_peer_list();
        }
    });
    return EC_Pending;
}


ErrorCode GSY_RequestConnectPeer(GSY_ConnectionHwnd handle, GSY_StreamId sid, GSY_PeerId peer_id,
    GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    if (conn->security_info().handshake != mqas::core::HandshakeState::Succeeded ||
        conn->security_info().authentication != mqas::core::PeerAuthentication::CertificatePin) return EC_Fail;
    push_task([conn,request_id, sid, handle, peer_id]()
    {
        const auto stream = check_stream_valid<P2PLobbyStreamPair,
            HolePunchingStream,GSY_LobbyStreamContext,ContextCheckCode>(handle,conn,sid,request_id);
        if (stream)
        {
            const auto s = stream->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();
            s->req_connect(peer_id);
        }
    });
    return EC_Pending;
}

ErrorCode GSY_RespondPeerConnectRequest(GSY_ConnectionHwnd handle, GSY_StreamId sid, GSY_PeerId peer_id,
                                               bool accept, GSY_RequestId request_id)
{
    CHECK_CONNECT_VALID(handle)
    if (accept && (conn->security_info().handshake != mqas::core::HandshakeState::Succeeded ||
        conn->security_info().authentication != mqas::core::PeerAuthentication::CertificatePin)) return EC_Fail;
    push_task([conn,request_id, sid, handle, peer_id,accept]()
    {
        const auto stream = check_stream_valid<P2PLobbyStreamPair,
            HolePunchingStream,GSY_LobbyStreamContext,ContextCheckCode>(handle,conn,sid,request_id);
        if (stream)
        {
            const auto s = stream->get_holds_stream<mqas::tools::p2p::P2PLobbyClientStream>();
            s->req_respond(peer_id, accept);
        }
    });
    return EC_Pending;
}

void set_address_data(GSY_sockaddr* addr,const mqas::tools::proto::p2p::Address& address)
{
    addr->port = address.port();
    if (address.ip().size() >= sizeof(addr->ip))
    {
        std::memset(addr->ip, 0, sizeof(addr->ip));
        return;
    }
    std::memcpy(addr->ip,address.ip().data(),address.ip().size());
    addr->ip[address.ip().size()] = '\0';
}

void set_result_data(GSY_HelperResult* result,
    const std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectResult>& msg,
    std::shared_ptr<mqas::io::UdpSocket> socket, GSY_ConnectionHwnd owner, GSY_StreamId sid, GSY_PeerId self_id)
{
    init_sockaddr_data(&result->address);
    init_sockaddr_data(&result->peer_addr);
    init_sockaddr_data(&result->relay_addr);

    result->ret = mapping_ret_code(msg->ret());
    result->session_handle = GSY_INVALID_SESSION_HANDLE;
    if (result->ret == EC_Ok) {
        std::shared_ptr<mqas::core::IConnect> lobby;
        {
            std::lock_guard<std::mutex> lock(connect_mutex);
            const auto it = connect_map.find(owner);
            if (it != connect_map.end()) lobby = it->second.lock();
        }
        if (!lobby || lobby->security_info().handshake != mqas::core::HandshakeState::Succeeded ||
            lobby->security_info().authentication != mqas::core::PeerAuthentication::CertificatePin) {
            result->ret = EC_Fail;
            return;
        }
        sockaddr peer_address{};
        if (!self_id || !msg->peer_id() || self_id == msg->peer_id() || !msg->has_peer_addr() ||
            msg->peer_addr().port() > 65535 || msg->peer_addr().port() == 0 ||
            msg->peer_addr().ip().size() >= sizeof(result->peer_addr.ip) ||
            msg->peer_addr().ip().find('\0') != std::string::npos ||
            !mqas::io::Ip::str2addr_ipv4(msg->peer_addr().ip().c_str(), static_cast<uint16_t>(msg->peer_addr().port()), peer_address) ||
            msg->psk_secret().size() != 32 || msg->psk_identity().size() != 16 ||
            !msg->psk_issued_at_ms() || msg->psk_expires_at_ms() <= msg->psk_issued_at_ms()) {
            result->ret = EC_Fail;
            return;
        }
        auto credential = std::make_shared<P2PSession>();
        std::memcpy(credential->secret.data(), msg->psk_secret().data(), credential->secret.size());
        std::memcpy(credential->identity.data(), msg->psk_identity().data(), credential->identity.size());
        credential->peer_id = msg->peer_id();
        credential->self_id = self_id;
        credential->is_server = msg->is_server();
        credential->peer_ip = msg->peer_addr().ip();
        credential->peer_port = static_cast<uint16_t>(msg->peer_addr().port());
        credential->issued_at_ms = msg->psk_issued_at_ms();
        credential->expires_at_ms = msg->psk_expires_at_ms();
        result->session_handle = p2p_session_register(owner, sid, std::move(socket), std::move(credential));
        if (result->session_handle == GSY_INVALID_SESSION_HANDLE) result->ret = EC_Fail;
    }
    result->peer_id = msg->peer_id();
    result->is_server = msg->is_server() ? 1 : 0;

    if (msg->has_peer_addr())
        set_address_data(&result->peer_addr, msg->peer_addr());

    result->reason = msg->reason().c_str();
    if (msg->has_address())
        set_address_data(&result->address, msg->address());
    if (msg->has_relay_addr())
        set_address_data(&result->relay_addr, msg->relay_addr());

    result->use_relay = msg->use_relay() ? 1 : 0;
    result->relay_token = msg->relay_token().c_str();
}

void init_helper_stream(std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream,
    GSY_LobbyStreamContext* context, GSY_StreamId stream_id, GSY_ConnectionHwnd owner, std::shared_ptr<GSY_PeerId> self_id)
{
    helper_stream->on_change_result.connect([context, stream_id](const std::shared_ptr<mqas::tools::proto::p2p::RespondConnectPeer>& msg)
    {
        if (context->on_change_to_helper_result)
            context->on_change_to_helper_result(stream_id,mapping_ret_code(msg->ret()), msg->peer_id());
    });
    helper_stream->on_connect_peer.connect([context, stream_id](const std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectPeerData>& msg)
    {
        if (context->on_attempt_connect)
            context->on_attempt_connect(stream_id, msg->connect_data().ip().c_str(), static_cast<uint16_t>(msg->connect_data().port()),msg->connect_data().send_times()
                , msg->connect_data().verify_code());
    });
    helper_stream->on_quit_result.connect([context, stream_id, owner,self_id](const std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectResult>& msg,std::shared_ptr<mqas::io::UdpSocket> socket)
    {
        if(context->on_helper_quit_result)
        {
            GSY_HelperResult result = {};
            set_result_data(&result,msg,std::move(socket),owner,stream_id,*self_id);
            context->on_helper_quit_result(stream_id, &result);
        }
        auto* secret = msg->mutable_psk_secret();
        if (!secret->empty()) OPENSSL_cleanse(secret->data(), secret->size());
        msg->clear_psk_secret();
    });

}
