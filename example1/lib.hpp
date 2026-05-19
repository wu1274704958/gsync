#pragma once
#include "def.h"
#include "mqas/core/def.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "p2p_datagram_stream.hpp"

constexpr uint32_t P2PLobbyStreamIndex = 1;
constexpr uint32_t P2PHelperStreamIndex = 2;
constexpr uint32_t P2PDatagramStreamIndex = 3;

//lobby
using P2PLobbyStreamPair = mqas::core::StreamVariantPair<P2PLobbyStreamIndex, mqas::tools::p2p::P2PLobbyClientStream>;
using P2PHelperStreamPair = mqas::core::StreamVariantPair<P2PHelperStreamIndex, mqas::tools::p2p::P2PHelperClientStream>;
//direct datagram
using P2PDatagramStreamPair = mqas::core::StreamVariantPair<P2PDatagramStreamIndex, P2PDatagramStream>;
//hole punching
using HolePunchingStream = mqas::core::StreamVariant<P2PLobbyStreamPair,P2PHelperStreamPair>;
//direct datagram
using P2PDatagramEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<mqas::core::StreamVariant<P2PDatagramStreamPair>>>>;
//hole punching
using HolePunchingEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<HolePunchingStream>>>;

using AllEngineType = std::tuple<HolePunchingEngine,P2PDatagramEngine>;

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

ErrorCode mapping_ret_code(mqas::tools::proto::p2p::RetCode ret_code);
void push_socket(std::shared_ptr<mqas::io::UdpSocket> sock);
std::shared_ptr<mqas::io::UdpSocket> get_socket(const size_t handle);

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
