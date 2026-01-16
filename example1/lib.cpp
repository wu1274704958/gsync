#include "example1.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../src/common.h"

constexpr uint32_t P2PLobbyStreamIndex = 1;
constexpr uint32_t P2PHelperStreamIndex = 2;

using P2PLobbyStreamPair = mqas::core::StreamVariantPair<P2PLobbyStreamIndex, mqas::tools::p2p::P2PLobbyClientStream>;
using P2PHelperStreamPair = mqas::core::StreamVariantPair<P2PHelperStreamIndex, mqas::tools::p2p::P2PHelperClientStream>;

using HolePunchingStream = mqas::core::StreamVariant<P2PLobbyStreamPair,P2PHelperStreamPair>;

using HolePunchingEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<HolePunchingStream>>>;

using AllEngineType = std::tuple<HolePunchingEngine>;

GSY_DEFINE_ENGINE_TYPE(AllEngineType)

constexpr uint8_t ContextCheckCode = 192;

ErrorCode mapping_ret_code(mqas::tools::proto::p2p::RetCode ret_code);

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
            stream = connect->make_stream();
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
        auto init_helper_stream = [context](std::shared_ptr<mqas::tools::p2p::P2PHelperClientStream> helper_stream)
        {

        };
        lobby_stream->on_change_helper_by_req = [stream, init_helper_stream](const mqas::tools::proto::p2p::ReqConnectPeer& msg)
        {
            stream->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqConnectPeerPair>(msg);
            init_helper_stream(stream->get_holds_stream<mqas::tools::p2p::P2PHelperClientStream>());
        };
        lobby_stream->on_change_helper = [stream, init_helper_stream](const mqas::tools::proto::p2p::ReqRespondPeerReqConnect& msg)
        {
            stream->req_change< mqas::tools::p2p::P2PHelperClientStream, mqas::tools::p2p::ReqRespondPeerReqConnectPair>(msg);
            init_helper_stream(stream->get_holds_stream<mqas::tools::p2p::P2PHelperClientStream>());
        };
        lobby_stream->on_register_signal.connect([context,stream_id](const std::shared_ptr<mqas::tools::proto::p2p::RespondRegistePeer>& msg)
        {
            if (context->on_registration_success)
                context->on_registration_success(mapping_ret_code(msg->ret()), msg->id(),stream_id);
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


#define CHECK_CONNECT_VALID(handle)                                                                             \
std::shared_ptr<mqas::core::IConnect> conn = nullptr;                                                           \
{                                                                                                               \
    std::lock_guard<std::mutex> lock(connect_mutex);                                                            \
    if (!connect_map.contains(handle) || connect_map.at(handle).expired())                                      \
        return EC_InvalidHandler;                                                                               \
    conn = connect_map.at(handle).lock();                                                                       \
    if (!conn)                                                                                                  \
        return EC_InvalidHandler;                                                                               \
}

template<typename SP,typename S,typename C,uint8_t CC>
requires requires
{
    requires std::is_default_constructible_v<S>;
    requires std::is_base_of_v<mqas::core::IStream, S>;
    requires mqas::core::variability_stream_pair_require<SP>;
}
std::shared_ptr<S> check_stream_valid(GSY_ConnectionHwnd handle,const std::shared_ptr<mqas::core::IConnect>& conn,GSY_StreamId sid,GSY_RequestId request_id,const std::source_location& location = std::source_location::current())
{
    const auto connect = std::static_pointer_cast<mqas::core::Connect<S>>(conn);
    const auto stream = connect->get_stream(reinterpret_cast<::lsquic_stream_t*>(sid));
    const auto cxt = static_cast<C*>(stream->get_cxt());
    if (cxt == nullptr || cxt->_check_code != CC)
    {
        LOG(ERROR) << location.function_name() << "LobbyStreamContext is invalid";
        return nullptr;
    }
    if (!stream)
    {
        if (cxt->on_error)
            cxt->on_error(handle,sid,EC_InvalidStream,"Not found stream",request_id);
        return nullptr;
    }
    if (stream->get_current_stream_tag() != SP::STREAM_TAG)
    {
        if (cxt->on_error)
            cxt->on_error(handle,sid,EC_WrongStreamState,"Current stream is not on lobby phase",request_id);
        return nullptr;
    }
    return stream;
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
ErrorCode mapping_ret_code(mqas::tools::proto::p2p::RetCode ret_code)
{
    switch (ret_code)
    {
    case mqas::tools::proto::p2p::RetCode::ok:
        return EC_Ok;
    case mqas::tools::proto::p2p::RetCode::already_exists:
        return EC_AlreadyExists;
    case mqas::tools::proto::p2p::RetCode::not_exists:
        return EC_NotExists;
    case mqas::tools::proto::p2p::RetCode::peer_rejected:
        return EC_PeerRejected;
    case mqas::tools::proto::p2p::RetCode::failed:
        return EC_Fail;
    default:
        return EC_Unknown;
    }
    return EC_Unknown;
}