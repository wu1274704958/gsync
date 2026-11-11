#include "lobby.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../src/common.h"
#include "lib.hpp"


GSY_DEFINE_ENGINE_TYPE(AllEngineType)

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
