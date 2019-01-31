#pragma once

#include "ae.h"
#include "anet.h"
#include "buffer.h"
#include "protocol.h"
#include <string>
#include <unistd.h>
#include <memory>
#include <unordered_map>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class session_t {
public:
    explicit session_t(int fd, const std::string& ip, int port)
    :fd_(fd),ip_(ip),port_(port)    
    {}
    ~session_t()
    {
    }
    int fd_{ -1 };
    std::string ip_;
    int port_;
    buffer_t read_buffer;
    buffer_t write_buffer;
};

// #define MAX_SESSIONS 100000
//#define BACKLOG 10240

// AF可选值AF_INET, AF_INET6
template <int AF = AF_INET, int MAX_SESSIONS = 100000, int BACKLOG = 10240>
class tcp_server {
public:
    int init(const std::string& bindaddr, int port,
        std::function<void(std::shared_ptr<session_t>)> accepted,
        std::function<void(std::shared_ptr<session_t>)> received,
        std::function<void(std::shared_ptr<session_t>)> disconnected)
    {
        accepted_ = accepted;
        received_ = received;
        disconnected_ = disconnected;

        loop = new aeEventLoop();
        loop->init(max_session_count);
        
        listen_fd = anetTcpServer(err_info, AF, bindaddr, port, backlog);
        if (listen_fd != ANET_ERR) {
            anetNonBlock(err_info, listen_fd);
        }

        if (loop->aeCreateFileEvent(listen_fd, AE_READABLE, std::bind(&tcp_server::acceptTcpHandler, this, std::placeholders::_1, std::placeholders::_2)) != AE_ERR) {
            char conn_info[64];
            anetFormatSock(listen_fd, conn_info, sizeof(conn_info));
            printf("listen on: %s\n", conn_info);
        }

        loop->aeMain();
        delete loop;

        return 0;
    }

    void acceptTcpHandler(int fd, int mask)
    {
        char cip[64];
        int cport;

        int cfd = anetTcpAccept(NULL, fd, cip, sizeof(cip), &cport);
        if (cfd != -1) {
            printf("accepted ip %s:%d\n", cip, cport);
            anetNonBlock(NULL, cfd);
            anetEnableTcpNoDelay(NULL, cfd);

            auto& session = session_dict_[cfd] = std::make_shared<session_t>(cfd, cip, cport);
            if (!session) {
                printf("alloc session error...close socket\n");
                close(fd);
                return;
            }  

            accepted_(session);
            if (loop->aeCreateFileEvent(cfd, AE_READABLE, std::bind(&tcp_server::readEventHandler, this, std::placeholders::_1, std::placeholders::_2)) == AE_ERR) {
                if (errno == ERANGE) {
                    printf("so many session, close new.");
                } else {
                    printf("create socket readable event error, close it.");
                }
                // delete session;
            }
        }
    }

    void writeEventHandler(int fd, int mask)
    {
        auto session = session_dict_[fd];
        buffer_t* wbuffer = &session->write_buffer;

        int data_size = (int)wbuffer->get_readable_size();
        if (data_size == 0) {
            loop->aeDeleteFileEvent(session->fd_, AE_WRITABLE);
            return;
        }

        int writen = anetWrite(session->fd_, (char*)wbuffer->buff + wbuffer->read_idx, data_size);
        if (writen > 0) {
            wbuffer->read_idx += writen;
        }
        if (wbuffer->get_readable_size() == 0) {
            loop->aeDeleteFileEvent(session->fd_, AE_WRITABLE);
        }
    }

    void readEventHandler(int fd, int mask)
    {
        auto session = session_dict_[fd];
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
                        int writen = anetWrite(session->fd_, (char*)wbuffer->buff + wbuffer->read_idx,
                            (int)wbuffer->get_readable_size());
                        if (writen > 0) {
                            wbuffer->read_idx += writen;
                        }
                        if (wbuffer->get_readable_size() != 0) {
                            if (loop->aeCreateFileEvent(session->fd_, AE_WRITABLE, std::bind(&tcp_server::writeEventHandler, this, std::placeholders::_1, std::placeholders::_2)) == AE_ERR) {
                                printf("create socket writeable event error, close it.");
                                // delete session;
                            }
                        }
                    }
                    delete req_package;
                    delete resp_package;
                } else if (decode_ret == -1) {
                    // not enough data
                    break;
                } else if (decode_ret == -2) {
                    char ip_info[64];
                    anetFormatPeer(session->fd_, ip_info, sizeof(ip_info));
                    printf("decode error, %s disconnect.\n", ip_info);
                    // recv wrong data, disconnect
                    // delete session;
                }
            }
        } else if (readn == 0) {
            printf("session disconnect, close it.\n");
            // delete session;

            if (fd > 0) {
                loop->aeDeleteFileEvent(fd, AE_READABLE);
                loop->aeDeleteFileEvent(fd, AE_WRITABLE);
                close(fd);
            }


        }
    }

    aeEventLoop* loop{ nullptr };
    int listen_fd{ 0 };
    int port{ 0 };
    int backlog{ BACKLOG };
    int max_session_count{ MAX_SESSIONS };
    char err_info[ANET_ERR_LEN];

    std::unordered_map<int, std::shared_ptr<session_t>> session_dict_;

    std::function<void(std::shared_ptr<session_t>)> accepted_;
    std::function<void(std::shared_ptr<session_t>)> received_;
    std::function<void(std::shared_ptr<session_t>)> disconnected_;
};

