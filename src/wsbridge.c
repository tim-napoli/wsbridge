#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>

#include "net.h"
#include "ws.h"
#include "client.h"

#define MAX_CLIENTS  32

socket_t ws_sock_g = SOCKET_ERROR;
client_t clients_g[MAX_CLIENTS] = {{0}};

/*
 * Returns the first non-alive client slot in `clients`, or NULL if there
 * is none.
 */
client_t* find_first_free_client_slot() {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (!clients_g[i].alive) {
            return clients_g + i;
        }
    }
    return NULL;
}


void sigint_handler(int signum) {
    if (ws_sock_g != SOCKET_ERROR) {
        printf("closing server socket\n");
        socket_gently_close(ws_sock_g);
    }
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients_g[i].alive) {
            clients_g[i].alive = false;
            pthread_join(clients_g[i].thread, NULL);
        }
    }
    exit(0);
}

int main(const int argc, const char** argv) {
    if (argc < 4) {
        printf(
            "usage: %s <listening port> <broadcast hostname> "
            "<broadcast port>\n",
            argv[0]
        );
        return 1;
    }

    const char* broadcast_hostname = argv[2];
    int listening_port;
    int broadcast_port;
    if (sscanf(argv[1], "%d", &listening_port) != 1) {
        printf("listening port '%s' is not a valid port format.\n", argv[2]);
        return 1;
    }
    if (sscanf(argv[3], "%d", &broadcast_port) != 1) {
        printf("broacast '%s' is not a valid port format.\n", argv[2]);
        return 1;
    }

    ws_sock_g = socket_create_server_tcp(listening_port, 32);
    if (ws_sock_g == SOCKET_ERROR) {
        return 1;
    }
    signal(SIGINT, &sigint_handler);

    while (1) {
        struct sockaddr client_addr;
        socklen_t addr_size = sizeof(struct sockaddr);
        int client_sock = accept(ws_sock_g, &client_addr, &addr_size);
        if (client_sock < 0) {
            printf("client connection failure\n");
        } else {
            printf("new client connected\n");

            client_t* client_slot = find_first_free_client_slot();
            if (!client_slot) {
                printf("no available client slot, rejecting\n");
                close(client_sock);
                continue;
            }

            client_init(client_slot, client_sock, broadcast_hostname,
                        broadcast_port);
            if (client_start(client_slot) == CLIENT_ERROR) {
                continue;
            }
        }
    }

    if (ws_sock_g != SOCKET_ERROR) {
        socket_gently_close(ws_sock_g);
    }

    return 0;
}
