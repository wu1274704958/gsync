#include "p2p_session.h"
#include <openssl/crypto.h>

P2PSession::~P2PSession() {
    OPENSSL_cleanse(secret.data(), secret.size());
    OPENSSL_cleanse(identity.data(), identity.size());
}
