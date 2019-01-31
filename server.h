#ifndef ANET_SERVER_H
#define ANET_SERVER_H

#include "define.h"
#include "anet.h"
#include "ae.h"
#include "buffer.h"

class tcp_server{
public:
    int init();
    aeEventLoop *loop{nullptr};
    int listen_fd{0};
    int port{0};
    int backlog{0};
    int max_session_count{0};
    char err_info[ANET_ERR_LEN];
};

class session_t {
public:
    ~session_t();
    aeEventLoop *loop {nullptr};
    int fd{-1};
    buffer_t read_buffer;
    buffer_t write_buffer;
} ;

#endif //ANET_SERVER_H
