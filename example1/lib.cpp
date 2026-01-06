#include "example1.h"
#include <utility>
#include "mqas/core/def.h"
#include "mqas/core/stream.h"
#include "mqas/core/sub_engine.h"
#include "mqas/tools/stream/p2p_lobby_client.h"
#include "mqas/tools/stream/p2p_helper_client.h"
#include "../src/common.h"

using HolePunchingStream = mqas::core::StreamVariant<
    mqas::core::StreamVariantPair<1, mqas::tools::p2p::P2PLobbyClientStream>,
    mqas::core::StreamVariantPair<2, mqas::tools::p2p::P2PHelperClientStream>>;

using HolePunchingEngine = mqas::core::sub_engine<mqas::core::engine<mqas::core::Connect<HolePunchingStream>>>;

using AllEngineType = std::tuple<HolePunchingEngine>;

GSY_DEFINE_ENGINE_TYPE(AllEngineType)
