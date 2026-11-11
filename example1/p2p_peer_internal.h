#pragma once
#include "p2p_peer.h"
#include <string>

struct P2PCredentials {
    GSY_PeerId self;
    GSY_PeerId other;
};
bool p2p_peer_credentials(GSY_ConnectionHwnd, P2PCredentials&);
