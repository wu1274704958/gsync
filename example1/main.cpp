#include <atomic>
#include <cstdint>
#include <cstdio>
#include "example1.h"
#include <mqas/log.h>

#ifndef CONFIG_PATH
#define CONFIG_PATH conf.txt
#endif

std::atomic<uint32_t> handle = 0;
std::atomic<uint32_t> self_id = 0;
std::atomic<uint64_t> stream = 0;

void error_callback(const char* msg,int error);
void connect_callback(int,unsigned int);
void disconnect_callback(int code,unsigned int id);
void connect_error_callback(int code,unsigned int id);
void request_connect_callback(struct PeerData*);
void register_to_lobby_error_callback(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,ErrorCode code, const char* msg,GSY_RequestId request_id);
void on_receive_peer_list_callback(GSY_PeerData* data,uint32_t size,GSY_StreamId stream_id);

void on_registration_success_callback(ErrorCode error_code, GSY_StreamId stream_id, GSY_PeerId peer_id);

int main()
{
    GSY_Context cxt{.on_error = error_callback};
    GSY_initialize(0,&cxt);

    GSY_BaseConnectionContext context{
        .on_connect = connect_callback,
        .on_disconnect = disconnect_callback,
        .on_error =  connect_error_callback,
    };

    GSY_LobbyStreamContext stream_context{
        .on_registration_success = on_registration_success_callback,
        .on_receive_peer_list = on_receive_peer_list_callback,
        .on_error = register_to_lobby_error_callback,
    };

    for(;;) {
        auto c = getchar();
        if (c == 'l') {
            handle = GSY_connect(1,CONFIG_PATH,"127.0.0.1",8084,&context);
        }
        if (c == 'd' && handle != 0) {
            GSY_disconnect(handle);
        }
        if (c == 'c') {
            auto res = GSY_is_connected(handle);
            LOG(INFO) << "hwnd:"<< handle << "is_connected: " << res;
        }
        if (c == 'm' && handle != 0)
        {
            stream = GSY_RegisterToLobby(handle,"aaa","",&stream_context);
            printf("register to lobby, stream id:%llu\n", stream.load(std::memory_order_relaxed));
        }
        if (c == 'p' && stream != 0)
        {
            GSY_FetchPeerList(handle,stream,1001);
        }
        if (c == 'u' && stream != 0)
        {
            GSY_UnregisterFromLobby(handle,stream,1002);
            stream = 0;
        }
        if (c == 'q') {
            break;
        }
    }

    GSY_terminate();
    return 0;
}

void error_callback(const char* msg,int error) {
    LOG(WARNING) << "on error,msg:"<< msg << " code:"<<error;
}
void connect_callback(int code,unsigned int id) {
    if (code != EC_Ok) {
        handle = 0;
    }else {
        handle = id;
    }
    LOG(INFO) << "on connected,id:"<< id << " code:"<< code;
}
void disconnect_callback(int code,unsigned int id) {
    handle = 0;
    self_id = 0;
    LOG(INFO) << "on disconnect,id:"<< id << " code:"<< code;
}

void connect_error_callback(int code,unsigned int id) {
    handle = 0;
    self_id = 0;
    LOG(INFO) << "on connect_error,id:"<< id << " code:"<< code;
}

void register_to_lobby_error_callback(GSY_ConnectionHwnd handle,GSY_StreamId stream_id,ErrorCode code, const char* msg,GSY_RequestId request_id)
{
    LOG(INFO) << "handle: "<< handle << " register to lobby got error:" << msg << ",request by " << request_id;
}

void on_receive_peer_list_callback(GSY_PeerData* data, uint32_t size, GSY_StreamId stream_id)
{
    LOG(INFO) << "on receive peer list size:" << size << ",stream id " << stream_id;
    for (uint32_t i = 0; i < size; i++)
    {
        LOG(INFO) << "peer " << i << " id:" << data[i].peer_id << " name:" << data[i].name;
    }
}

void on_registration_success_callback(ErrorCode error_code, GSY_StreamId stream_id, GSY_PeerId peer_id)
{
    printf("on registration success, code:%d, stream_id:%llu, peer_id:%u\n", error_code, stream_id, peer_id);
}
