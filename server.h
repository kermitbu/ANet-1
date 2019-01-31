#ifndef ANET_SERVER_H
#define ANET_SERVER_H

#include "define.h"
#include "anet.h"
#include "ae.h"
#include <unistd.h>
#include "buffer.h"
#include <string>
#include "buffer.h"
#include "protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class session_t {
public:
~session_t()
{
    if (fd > 0) {
        loop->aeDeleteFileEvent(fd, AE_READABLE);
        loop->aeDeleteFileEvent(fd, AE_WRITABLE);
        close(fd);
    }
}
    aeEventLoop *loop {nullptr};
    int fd{-1};
    buffer_t read_buffer;
    buffer_t write_buffer;
} ;

static void acceptTcpHandler(aeEventLoop* loop, int fd, void* data, int mask);
static void readEventHandler(aeEventLoop* loop, int fd, void* data, int mask);
static void writeEventHandler(aeEventLoop* loop, int fd, void* data, int mask);

#define  MAX_SESSIONS 100000
#define BACKLOG 10240
// template <int AF = AF_INET, int MAX_SESSIONS = 100000, int BACKLOG = 10240>
class tcp_server{
public:
    int init(const std::string& host, int port)
    {

    loop = new aeEventLoop();
    loop->init(max_session_count);

    listen_fd = anetTcpServer(err_info, port, NULL, backlog);
    if (listen_fd != ANET_ERR) {
        anetNonBlock(err_info, listen_fd);
    }

    if (loop->aeCreateFileEvent(listen_fd, AE_READABLE, acceptTcpHandler, this) != AE_ERR) {
        char conn_info[64];
        anetFormatSock(listen_fd, conn_info, sizeof(conn_info));
        printf("listen on: %s\n", conn_info);
    }

    loop->aeMain();
    delete loop;

    return 0;
}

    aeEventLoop *loop{nullptr};
    int listen_fd{0};
    int port{0};
    int backlog{BACKLOG};
    int max_session_count{MAX_SESSIONS};
    char err_info[ANET_ERR_LEN];
};


static void writeEventHandler(aeEventLoop* loop, int fd, void* data, int mask)
{
    session_t* session = (session_t*)data;
    buffer_t* wbuffer = &session->write_buffer;

    int data_size = (int)wbuffer->get_readable_size();
    if (data_size == 0) {
        session->loop->aeDeleteFileEvent(session->fd, AE_WRITABLE);
        return;
    }

    int writen = anetWrite(session->fd, (char*)wbuffer->buff + wbuffer->read_idx, data_size);
    if (writen > 0) {
        wbuffer->read_idx += writen;
    }
    if (wbuffer->get_readable_size() == 0) {
        session->loop->aeDeleteFileEvent(session->fd, AE_WRITABLE);
    }
}

static void readEventHandler(aeEventLoop* loop, int fd, void* data, int mask)
{
    session_t* session = (session_t*)data;
    buffer_t* rbuffer = &session->read_buffer;

    rbuffer->check_buffer_size(DEFAULT_BUFF_SIZE / 2);

    size_t avlid_size = rbuffer->size - rbuffer->write_idx;
    ssize_t readn = read(fd, rbuffer->buff + rbuffer->write_idx, avlid_size);

    if (readn > 0) {
        rbuffer->write_idx += readn;
        package_t* req_package = NULL;
        while (1) {
            int decode_ret = packet_decode(rbuffer, &req_package);
            if (decode_ret == 0) {
                package_t* resp_package = NULL;
                do_package(req_package, &resp_package);
                if (resp_package) {
                    buffer_t* wbuffer = &session->write_buffer;
                    wbuffer->check_buffer_size(sizeof(package_head_t) + resp_package->head.length);
                    package_encode(wbuffer, resp_package);
                    int writen = anetWrite(session->fd, (char*)wbuffer->buff + wbuffer->read_idx,
                        (int)wbuffer->get_readable_size());
                    if (writen > 0) {
                        wbuffer->read_idx += writen;
                    }
                    if (wbuffer->get_readable_size() != 0) {
                        if (session->loop->aeCreateFileEvent(session->fd, AE_WRITABLE, writeEventHandler, session) == AE_ERR) {
                            printf("create socket writeable event error, close it.");
                            delete session;
                        }
                    }
                }
                zfree(req_package);
                zfree(resp_package);
            } else if (decode_ret == -1) {
                // not enough data
                break;
            } else if (decode_ret == -2) {
                char ip_info[64];
                anetFormatPeer(session->fd, ip_info, sizeof(ip_info));
                printf("decode error, %s disconnect.\n", ip_info);
                // recv wrong data, disconnect
                delete session;
            }
        }
    } else if (readn == 0) {
        printf("session disconnect, close it.\n");
        delete session;
    }
}

static void acceptTcpHandler(aeEventLoop* loop, int fd, void* data, int mask)
{
    char cip[64];
    int cport;

    tcp_server* server = (tcp_server*)data;

    int cfd = anetTcpAccept(NULL, fd, cip, sizeof(cip), &cport);
    if (cfd != -1) {
        printf("accepted ip %s:%d\n", cip, cport);
        anetNonBlock(NULL, cfd);
        anetEnableTcpNoDelay(NULL, cfd);
        session_t* session = new session_t();
        if (!session) {
            printf("alloc session error...close socket\n");
            close(fd);
            return;
        }

        session->loop = loop;
        session->fd = cfd;

        if (loop->aeCreateFileEvent(cfd, AE_READABLE, readEventHandler, session) == AE_ERR) {
            if (errno == ERANGE) {
                printf("so many session, close new.");
            } else {
                printf("create socket readable event error, close it.");
            }
            delete session;
        }
    }
}

#endif //ANET_SERVER_H
