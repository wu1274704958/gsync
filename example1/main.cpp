#include <cstdio>
#include "example1.h"

#ifndef CONFIG_PATH
#define CONFIG_PATH conf.txt
#endif


void error_callback(const char* msg,int error);
void connect_callback(int,unsigned int);
void request_connect_callback(struct PeerData*);
int main()
{
    GSY_initialize(0,error_callback);

    for(;;) {
        auto c = getchar();
        if (c == 'l') {
            GSY_connect_hole_punching_server(CONFIG_PATH,"aaa","aaa",connect_callback,request_connect_callback);
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
    printf("on connect = id = %ud code = %d\n",id,code);
}
void request_connect_callback(struct PeerData* peer) {
    printf("on recv request name = %d:%s",peer->id,peer->name);
}