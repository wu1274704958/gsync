#include <mqas/tools/stream/p2p_direct_stream.h>
#include <cassert>
using mqas::tools::p2p_direct::DirectStreamRegistry;

int main() {
    DirectStreamRegistry a, b;
    int one, two, three;
    void* found = nullptr;
    uint32_t local = 0, peer = 0;
    assert(!a.reserve_local(&one, 0));
    auto first = a.reserve_local(&one, 11);
    assert(first && !a.reserve_local(&two, 11));
    assert(!a.find_stream(&one, local, peer));
    auto pending = a.reserve_peer(&two, 22);
    assert(pending && !a.reserve_peer(&three, 22));
    assert(!a.find_peer(22, found));
    pending.reset();
    assert(a.complete(first, 11, 21));
    assert(a.find_peer(21, found) && found == &one);
    assert(a.find_stream(&one, local, peer) && local == 11 && peer == 21);
    auto second = a.reserve_local(&two, 12);
    assert(second && !a.complete(second, 12, 21));
    assert(!a.reserve_local(&three, 12));
    auto separate = b.reserve_local(&three, 11);
    assert(separate && b.complete(separate, 11, 21));
    first.reset();
    assert(!a.find_peer(21, found));
    auto replacement = a.reserve_local(&one, 11);
    assert(replacement && a.complete(replacement, 11, 21));
    // A stale entry cannot be completed against the replacement or erase it.
    auto old = second;
    second.reset();
    assert(a.complete(old, 12, 23));
    old.reset();
    auto renewed = a.reserve_local(&two, 12);
    assert(renewed && a.complete(renewed, 12, 23));
    assert(a.find_peer(23, found) && found == &two);
    std::shared_ptr<DirectStreamRegistry::Entry> survivor;
    {
        DirectStreamRegistry temporary;
        survivor = temporary.reserve_local(&three, 44);
        assert(survivor);
    }
    survivor.reset();
}
