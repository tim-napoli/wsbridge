#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
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
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   &(int){ 1 }, sizeof(int)) < 0)
    {
        fprintf(stderr, "server socket will not be reusable\n");
    }
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

socket_t socket_create_client_tcp(const char* hostname, int port) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent* hostinfo;
    struct sockaddr_in sin;

    hostinfo = gethostbyname(hostname);
    if (!hostinfo) {
        fprintf(stderr, "unknown host %s\n", hostname);
        return SOCKET_ERROR;
    }

    sin = (struct sockaddr_in){
        .sin_addr = *(struct in_addr*)hostinfo->h_addr,
        .sin_port = htons(port),
        .sin_family = AF_INET
    };
    if (connect(sock, (struct sockaddr*)&sin, sizeof(struct sockaddr)) < 0) {
        fprintf(stderr, "cannot connect to %s:%d\n", hostname, port);
        return SOCKET_ERROR;
    }

    return sock;
}

void socket_flush(socket_t sock) {
    char buf[4096];
    while (recv(sock, buf, sizeof(buf), 0) > 0) {
        /* nothing */
    }
}

void socket_close(socket_t sock) {
    close(sock);
}

void socket_gently_close(socket_t sock) {
    shutdown(sock, SHUT_RD);

    char buf[4096];
    while (recv(sock, buf, sizeof(buf) - 1, 0) > 0) {
        /* noting */
    }

    close(sock);
}
