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

void error_callback(const char* msg,int error);
void connect_callback(int,unsigned int);
void disconnect_callback(int code,unsigned int id);
void connect_error_callback(int code,unsigned int id);
void request_connect_callback(struct PeerData*);
int main()
{
    GSY_Context cxt{.on_error = error_callback};
    GSY_initialize(0,&cxt);

    GSY_BaseConnectionContext context{
        .on_connect = connect_callback,
        .on_disconnect = disconnect_callback,
        .on_error =  connect_error_callback,
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
        self_id = id;
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
