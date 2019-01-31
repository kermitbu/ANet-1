#include "server.h"
#include "buffer.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

session_t *alloc_session()
{
    session_t * session = new session_t();
    if (session == NULL) {
        goto err;
    }
    session->loop = NULL;
    session->fd = -1;
    session->read_buffer;
    session->write_buffer;

    return session;

err:
    return NULL;
}

void free_session(session_t *session)
{
    if (session) {
        if (session->fd > 0) {
            session->loop->aeDeleteFileEvent(session->fd, AE_READABLE);
            session->loop->aeDeleteFileEvent(session->fd, AE_WRITABLE);
            close(session->fd);
        }
        zfree(session);
    }
}

static int serverCron(struct aeEventLoop *loop, long long id, void *data)
{
    static int count = 1;
    printf("timeout, No.%d\n", count);

    if (++count > 3) {
        printf("exit now...");
        loop->stop();
    }

    return 1000;
}

static void writeEventHandler(aeEventLoop *loop, int fd, void *data, int mask)
{
    session_t *session = (session_t *)data;
    buffer_t *wbuffer = &session->write_buffer;

    int data_size = (int)wbuffer->get_readable_size();
    if (data_size == 0) {
        session->loop->aeDeleteFileEvent(session->fd, AE_WRITABLE);
        return;
    }

    int writen = anetWrite(session->fd, (char *)wbuffer->buff + wbuffer->read_idx, data_size);
    if (writen > 0) {
        wbuffer->read_idx += writen;
    }
    if (wbuffer->get_readable_size() == 0) {
        session->loop->aeDeleteFileEvent(session->fd, AE_WRITABLE);
    }
}

static void readEventHandler(aeEventLoop *loop, int fd, void *data, int mask)
{
    session_t *session = (session_t *)data;
    buffer_t *rbuffer = &session->read_buffer;

    rbuffer->check_buffer_size(DEFAULT_BUFF_SIZE / 2);

    size_t avlid_size = rbuffer->size - rbuffer->write_idx;
    ssize_t readn = read(fd, rbuffer->buff + rbuffer->write_idx, avlid_size);

    if (readn > 0) {
        rbuffer->write_idx += readn;
        package_t *req_package = NULL;
        while (1) {
            int decode_ret = packet_decode(rbuffer, &req_package);
            if (decode_ret == 0) {
                package_t *resp_package = NULL;
                do_package(req_package, &resp_package);
                if (resp_package) {
                    buffer_t *wbuffer = &session->write_buffer;
                    wbuffer->check_buffer_size(sizeof(package_head_t) + resp_package->head.length);
                    package_encode(wbuffer, resp_package);
                    int writen = anetWrite(session->fd, (char *)wbuffer->buff + wbuffer->read_idx,
                                           (int)wbuffer->get_readable_size());
                    if (writen > 0) {
                        wbuffer->read_idx += writen;
                    }
                    if (wbuffer->get_readable_size() != 0) {
                        if (session->loop->aeCreateFileEvent(session->fd,
                                              AE_WRITABLE, writeEventHandler, session) == AE_ERR) {
                            printf("create socket writeable event error, close it.");
                            free_session(session);
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
                free_session(session);
            }
        }
    } else if (readn == 0) {
        printf("session disconnect, close it.\n");
        free_session(session);
    }
}

static void acceptTcpHandler(aeEventLoop *loop, int fd, void *data, int mask)
{
    char cip[64];
    int cport;

    server_t *server = (server_t *)data;

    int cfd = anetTcpAccept(NULL, fd, cip, sizeof(cip), &cport);
    if (cfd != -1) {
        printf("accepted ip %s:%d\n", cip, cport);
        anetNonBlock(NULL, cfd);
        anetEnableTcpNoDelay(NULL, cfd);
        session_t *session = alloc_session();
        if (!session) {
            printf("alloc session error...close socket\n");
            close(fd);
            return;
        }

        session->loop = loop;
        session->fd = cfd;

        if (loop->aeCreateFileEvent(cfd, AE_READABLE, readEventHandler, session) == AE_ERR) {
            if (errno == ERANGE) {
                // or use aeResizeSetSize(server->loop, cfd) modify this limit
                printf("so many session, close new.");
            } else {
                printf("create socket readable event error, close it.");
            }
            free_session(session);
        }
    }
}

void init_server(server_t *server)
{
    server->loop = new aeEventLoop();
    server->loop->init(server->max_session_count);

    server->listen_fd = anetTcpServer(server->err_info, server->port, NULL, server->backlog);
    if (server->listen_fd != ANET_ERR) {
        anetNonBlock(server->err_info, server->listen_fd);
    }

    if (server->loop->aeCreateFileEvent(server->listen_fd, AE_READABLE, acceptTcpHandler, server) != AE_ERR) {
        char conn_info[64];
        anetFormatSock(server->listen_fd, conn_info, sizeof(conn_info));
        printf("listen on: %s\n", conn_info);
    }
}

void wait_server(server_t *server)
{
    server->loop->aeMain();
    delete server->loop;
}

int main()
{
    // TODO, use sigaction or signalfd
    signal(SIGPIPE, SIG_IGN);
    server_t server;
    bzero(&server, sizeof(server));

    server.backlog = DEFAULT_LISTEN_BACKLOG;
    server.max_session_count = DEFAULT_MAX_session_COUNT;
    server.port = DEFAULT_LISTEN_PORT;

    init_server(&server);
    wait_server(&server);

    return 0;
}
