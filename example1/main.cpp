#include <atomic>
#include <cstdint>
#include <cstdio>
#include "example1.h"

#ifndef CONFIG_PATH
#define CONFIG_PATH conf.txt
#endif

std::atomic<uint32_t> handler = 0;
std::atomic<uint32_t> self_id = 0;

void error_callback(const char* msg,int error);
void connect_callback(int,unsigned int);
void disconnect_callback(int code,unsigned int id);
void connect_error_callback(int code,unsigned int id);
void request_connect_callback(struct PeerData*);
int main()
{
    GSY_initialize(0,GSY_Context{.on_error = error_callback});

    for(;;) {
        auto c = getchar();
        if (c == 'l') {
            handler = GSY_connect_hole_punching_server(CONFIG_PATH,"aaa","aaa",GSY_HPConnectContext{
                .on_connect = connect_callback,
                .on_disconnect = disconnect_callback,
                .on_error = connect_error_callback,
                .on_request_connect = nullptr,
                .on_response_connect = nullptr,
            });
        }
        if (c == 'd' && handler != 0) {
            GSY_disconnect_hole_punching_server(handler);
        }
        if (c == 'q') {
            break;
        }
    }

    GSY_terminate();
    return 0;
}

void error_callback(const char* msg,int error) {
    printf("error = %s code = %d\n",msg,error);
}
void connect_callback(int code,unsigned int id) {
    if (code != EC_Ok) {
        handler = 0;
    }else {
        self_id = id;
    }
    printf("on connect = id = %u code = %d\n",id,code);
}
void disconnect_callback(int code,unsigned int id) {
    handler = 0;
    self_id = 0;
    printf("on disconnect = id = %u code = %d\n",id,code);
}

void connect_error_callback(int code,unsigned int id) {
    handler = 0;
    self_id = 0;
    printf("on connect_error = id = %u code = %d\n",id,code);
}
