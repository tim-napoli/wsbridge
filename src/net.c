#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "net.h"

net_status_t socket_set_non_blocking(socket_t sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        return NET_ERROR;
    }
    return NET_SUCCESS;
}

socket_t socket_create_server_tcp(int port, size_t max_connections) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };

    if (bind(sock,
             (const struct sockaddr*)&addr,
             sizeof(struct sockaddr_in))
        < 0)
    {
        fprintf(stderr, "unable to bind listening socket on %d\n", port);
        return SOCKET_ERROR;
    }

    if (listen(sock, max_connections) < 0) {
        fprintf(stderr, "unable to listen for %zu connections\n",
                max_connections);
        return SOCKET_ERROR;
    }

    return sock;
}

