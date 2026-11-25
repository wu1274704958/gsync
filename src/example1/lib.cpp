#include "gsync/example1/lobby.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../core/common.h"
#include "lib.hpp"
#include "engine_stages.hpp"


GSY_DEFINE_ENGINE_TYPE(AllEngineType, gsync::stages::ConnectConfig, gsync::stages::ServerConfig)

ErrorCode GSY_connect_session(const char* config_file, GSY_SessionHandle session,
    GSY_BaseConnectionContext* context, GSY_ConnectionHwnd* out_connection)
{
    if (out_connection) *out_connection = InvalidConnection;
    if (!is_running) return EC_NotInitialized;
    if (!out_connection) return EC_Fail;
    const GSY_P2PConnectOptions options{{GSY_CONTEXT_P2P_CONNECT, sizeof(GSY_P2PConnectOptions)}, session};
    return gsync::stages::connect_internal<0, AllEngineType, gsync::stages::ConnectConfig>(
        2, config_file, nullptr, 0, context, &options.header, *out_connection);
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
