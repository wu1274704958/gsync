#include "lobby.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../src/common.h"
#include "lib.hpp"


constexpr uint8_t ContextCheckCode = 192;

void init_helper_stream(std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream, GSY_LobbyStreamContext* context, GSY_StreamId stream_id);

GSY_StreamId GSY_RegisterToLobby(GSY_ConnectionHwnd handle,const char* name,const char* psd,GSY_LobbyStreamContext* context)
{
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
    return push_task_with_result<GSY_StreamId>([conn,context,name, handle]() -> GSY_StreamId
    {
        const auto connect = std::static_pointer_cast<mqas::core::Connect<HolePunchingStream>>(conn);
        std::shared_ptr<HolePunchingStream> stream = nullptr;
        if (const auto it = connect->enumerate_stream();it != connect->enumerate_stream_end())
        {
            stream = it->second;
            if (stream->get_current_stream_tag() != 0)
            {
                const auto sid = reinterpret_cast<GSY_StreamId>(stream->get_origin());
                if (context->on_error)
                    context->on_error(handle,sid,EC_WrongStreamState,"Already registered",NONE_RID);
                return sid;
            }
        }else
        {
            std::atomic_bool stream_ready = false;
            sigc::connection signal_connection = connect->make_stream( [&stream,&stream_ready](std::shared_ptr<HolePunchingStream> s)
            {
                stream = std::move(s);
                stream_ready.store(true, std::memory_order_release);
            });
            while (!stream_ready.load(std::memory_order_acquire)){}
            signal_connection.disconnect();
            stream->set_cxt(context);
        }

        if (stream == nullptr)
        {
            if (context->on_error)
                context->on_error(handle,INVALID_SID,EC_MakeStreamFailed,"Make stream failed",NONE_RID);
            return INVALID_PID;
        }
        mqas::tools::proto::p2p::ReqRegistePeer msg;
        msg.set_name(name);
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
        auto init_helper_stream_func = [context,stream_id](std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream)
        {
            init_helper_stream(std::move(helper_stream), context, stream_id);
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
        lobby_stream->on_register_signal.connect([context,stream_id](const std::shared_ptr<mqas::tools::proto::p2p::RespondRegistePeer>& msg)
        {
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
    });
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
    if (address.ip().size() + 1 > sizeof(addr->ip))
        std::memset(addr->ip, 0, sizeof(addr->ip));
    std::strncpy(addr->ip,address.ip().c_str(),address.ip().size());
    addr->ip[address.ip().size()] = '\0';
}

void set_result_data(GSY_HelperResult* result,
    const std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectResult>& msg,
    std::shared_ptr<mqas::io::UdpSocket> socket)
{
    auto sock_handle = socket.get();
    push_socket(std::move(socket));

    init_sockaddr_data(&result->address);
    init_sockaddr_data(&result->peer_addr);
    init_sockaddr_data(&result->relay_addr);

    result->socket_handle = reinterpret_cast<size_t>(sock_handle);
    result->ret = mapping_ret_code(msg->ret());
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
    GSY_LobbyStreamContext* context, GSY_StreamId stream_id)
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
    helper_stream->on_quit_result.connect([context, stream_id](const std::shared_ptr<mqas::tools::proto::p2p::NotifyConnectResult>& msg,std::shared_ptr<mqas::io::UdpSocket> socket)
    {
        if(context->on_helper_quit_result)
        {
            GSY_HelperResult result = {};
            set_result_data(&result,msg,std::move(socket));
            context->on_helper_quit_result(stream_id, &result);
        }
    });

}
