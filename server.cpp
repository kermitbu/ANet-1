#include "server.h"
#include "buffer.h"
#include "protocol.h"
#include <functional>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void on_accept_connection(std::shared_ptr<session_t> session){
    printf("accapt a connection from %s:%d\n", session->ip_.c_str(), session->port_);
}

void on_receive_data(std::shared_ptr<session_t> session){
    printf("received a package from %s:%d\n", session->ip_.c_str(), session->port_);
}

void on_disconnected(std::shared_ptr<session_t> session){
    printf("disconnect %s:%d\n", session->ip_.c_str(), session->port_);
}

int main()
{
    // TODO, use sigaction or signalfd
    signal(SIGPIPE, SIG_IGN);
    tcp_server<> server;
    
    server.init("127.0.0.1", 12345,
    std::bind(on_accept_connection, std::placeholders::_1),
        std::bind(on_receive_data, std::placeholders::_1),
        std::bind(on_disconnected, std::placeholders::_1)
    );

    return 0;
}
