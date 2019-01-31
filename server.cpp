#include "server.h"
#include "buffer.h"
#include "protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main()
{
    // TODO, use sigaction or signalfd
    signal(SIGPIPE, SIG_IGN);
    tcp_server server;
    
    server.init("127.0.0.1", DEFAULT_LISTEN_PORT);

    return 0;
}
