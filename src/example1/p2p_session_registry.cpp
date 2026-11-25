#include "p2p_session_registry.h"
#include "p2p_session.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
namespace {
struct Entry {
    GSY_ConnectionHwnd owner;
    GSY_StreamId stream;
    std::weak_ptr<mqas::io::UdpSocket> socket;
    std::shared_ptr<const P2PSession> session;
    bool in_use = false;
    bool server_registered = false;
};
std::mutex entries_mutex;
std::unordered_map<GSY_SessionHandle, Entry> entries;
GSY_SessionHandle next_handle = 1;
std::unordered_set<std::string> early_ready;
std::unordered_set<GSY_SessionHandle> ready;
std::string ready_key(GSY_ConnectionHwnd owner, GSY_StreamId stream, uint32_t peer, const std::string& id) {
    return std::to_string(owner) + ":" + std::to_string(stream) + ":" + std::to_string(peer) + ":" + id;
}
}
GSY_SessionHandle p2p_session_register(GSY_ConnectionHwnd owner, GSY_StreamId stream,
    const std::shared_ptr<mqas::io::UdpSocket>& socket, std::shared_ptr<const P2PSession> session) {
    if (!socket || !session) return GSY_INVALID_SESSION_HANDLE;
    std::lock_guard lock(entries_mutex);
    if (next_handle == GSY_INVALID_SESSION_HANDLE) return GSY_INVALID_SESSION_HANDLE;
    const auto handle = next_handle++;
    entries.emplace(handle, Entry{owner, stream, socket, std::move(session)});
    const auto& s = *entries.at(handle).session;
    const std::string id(reinterpret_cast<const char*>(s.negotiation_id.data()), s.negotiation_id.size());
    if (!s.is_server && early_ready.erase(ready_key(owner, stream, s.peer_id, id))) ready.insert(handle);
    return handle;
}
ErrorCode p2p_session_ready_data(GSY_SessionHandle handle, GSY_ConnectionHwnd owner, GSY_StreamId stream, std::shared_ptr<const P2PSession>& session) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end() || it->second.owner != owner || it->second.stream != stream) return EC_InvalidHandler;
    if (!it->second.in_use || !it->second.server_registered || !it->second.session->is_server || !it->second.session->valid_now()) return EC_WrongStreamState;
    session = it->second.session; return EC_Ok;
}
bool p2p_session_mark_server_registered(GSY_SessionHandle handle, const std::shared_ptr<const P2PSession>& session) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end() || !it->second.in_use || it->second.session != session) return false;
    it->second.server_registered = true;
    return true;
}
GSY_SessionHandle p2p_session_mark_ready(GSY_ConnectionHwnd owner, GSY_StreamId stream, uint32_t peer, const std::string& id) {
    if (id.size() != 16 || !peer) return GSY_INVALID_SESSION_HANDLE;
    std::lock_guard lock(entries_mutex);
    for (const auto& [handle, entry] : entries) {
        const auto& s = *entry.session;
        if (entry.owner == owner && entry.stream == stream && !s.is_server && s.peer_id == peer && s.valid_now() &&
            id == std::string(reinterpret_cast<const char*>(s.negotiation_id.data()), s.negotiation_id.size())) return handle;
    }
    if (early_ready.size() < 1024) early_ready.insert(ready_key(owner, stream, peer, id));
    return GSY_INVALID_SESSION_HANDLE;
}
bool p2p_session_take_ready(GSY_SessionHandle handle) {
    std::lock_guard lock(entries_mutex); return ready.erase(handle) != 0;
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
    if (it != entries.end() && it->second.session == session) { it->second.in_use = false; it->second.server_registered = false; }
}
bool p2p_session_consume(GSY_SessionHandle handle, const std::shared_ptr<const P2PSession>& session) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end() || !it->second.in_use || it->second.session != session) return false;
    ready.erase(handle); entries.erase(it);
    return true;
}
ErrorCode p2p_session_remove(GSY_SessionHandle handle) {
    std::lock_guard lock(entries_mutex);
    const auto it = entries.find(handle);
    if (it == entries.end()) return EC_InvalidHandler;
    if (it->second.in_use) return EC_SessionBusy;
    ready.erase(handle); entries.erase(it);
    return EC_Ok;
}
void p2p_session_remove_stream(GSY_ConnectionHwnd owner, GSY_StreamId stream) {
    std::lock_guard lock(entries_mutex);
    for (auto it = entries.begin(); it != entries.end();)
        if (it->second.owner == owner && it->second.stream == stream) { ready.erase(it->first); it = entries.erase(it); }
        else ++it;
    const auto prefix = std::to_string(owner) + ":" + std::to_string(stream) + ":";
    std::erase_if(early_ready, [&prefix](const auto& key) { return key.starts_with(prefix); });
}
void p2p_session_remove_connection(GSY_ConnectionHwnd owner) {
    std::lock_guard lock(entries_mutex);
    for (auto it = entries.begin(); it != entries.end();)
        if (it->second.owner == owner) { ready.erase(it->first); it = entries.erase(it); } else ++it;
    const auto prefix = std::to_string(owner) + ":";
    std::erase_if(early_ready, [&prefix](const auto& key) { return key.starts_with(prefix); });
}
void p2p_session_shutdown() {
    std::lock_guard lock(entries_mutex);
    entries.clear();
    ready.clear(); early_ready.clear();
}
