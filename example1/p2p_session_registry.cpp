#include "p2p_session_registry.h"
#include <mutex>
#include <unordered_map>
namespace {
struct Entry {
    GSY_ConnectionHwnd owner;
    GSY_StreamId stream;
    std::weak_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<const P2PSession> session;
    bool in_use = false;
};
std::mutex entries_mutex;
std::unordered_map<GSY_SessionHandle, Entry> entries;
GSY_SessionHandle next_handle = 1;
}
GSY_SessionHandle p2p_session_register(GSY_ConnectionHwnd owner, GSY_StreamId stream,
    const std::shared_ptr<mqas::io::UdpSocket>& socket, std::shared_ptr<const P2PSession> session) {
    if (!socket || !session) return GSY_INVALID_SESSION_HANDLE;
    std::lock_guard lock(entries_mutex);
    if (next_handle == GSY_INVALID_SESSION_HANDLE) return GSY_INVALID_SESSION_HANDLE;
    const auto handle = next_handle++;
    entries.emplace(handle, Entry{owner, stream, socket, std::move(session)});
    return handle;
}
ErrorCode p2p_session_acquire(GSY_SessionHandle handle, std::shared_ptr<mqas::io::UdpSocket>& socket,
    std::shared_ptr<const P2PSession>& session) {
    socket.reset(); session.reset();
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end()) return EC_InvalidHandler;
    if (it->second.in_use) return EC_SessionBusy;
    socket = it->second.socket.lock();
    if (!socket) { entries.erase(it); return EC_InvalidHandler; }
    session = it->second.session;
    it->second.in_use = true;
    return EC_Ok;
}
void p2p_session_release_attempt(GSY_SessionHandle handle, const std::shared_ptr<const P2PSession>& session) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it != entries.end() && it->second.session == session) it->second.in_use = false;
}
bool p2p_session_consume(GSY_SessionHandle handle, const std::shared_ptr<const P2PSession>& session) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end() || !it->second.in_use || it->second.session != session) return false;
    entries.erase(it);
    return true;
}
ErrorCode p2p_session_remove(GSY_SessionHandle handle) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end()) return EC_InvalidHandler;
    if (it->second.in_use) return EC_SessionBusy;
    entries.erase(it);
    return EC_Ok;
}
void p2p_session_remove_stream(GSY_ConnectionHwnd owner, GSY_StreamId stream) {
    std::lock_guard lock(entries_mutex);
    for (auto it = entries.begin(); it != entries.end();)
        if (it->second.owner == owner && it->second.stream == stream) it = entries.erase(it);
        else ++it;
}
void p2p_session_remove_connection(GSY_ConnectionHwnd owner) {
    std::lock_guard lock(entries_mutex);
    for (auto it = entries.begin(); it != entries.end();)
        if (it->second.owner == owner) it = entries.erase(it); else ++it;
}
void p2p_session_shutdown() {
    std::lock_guard lock(entries_mutex);
    entries.clear();
}
