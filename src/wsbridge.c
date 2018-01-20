#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

#include "net.h"
#include "ws.h"
#include "client.h"

#define MAX_CONNECTIONS  32

/*
 * Returns the first non-alive client slot in `clients`, or NULL if there
 * is none.
 */
client_t* find_first_free_client_slot(client_t* clients, size_t clients_count) {
    for (size_t i = 0; i < clients_count; i++) {
        if (!clients[i].alive) {
            return clients + i;
        }
    }
    return NULL;
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

    socket_t ws_sock = socket_create_server_tcp(listening_port, 32);
    if (ws_sock == SOCKET_ERROR) {
        return 1;
    }

    client_t clients[MAX_CONNECTIONS];
    memset(clients, 0, sizeof(clients));
    while (1) {
        struct sockaddr client_addr;
        socklen_t addr_size = sizeof(struct sockaddr);
        int client_sock = accept(ws_sock, &client_addr, &addr_size);
        if (client_sock < 0) {
            printf("client connection failure\n");
        } else {
            printf("new client connected\n");

            client_t* client_slot = find_first_free_client_slot(
                clients, MAX_CONNECTIONS
            );
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

    close(ws_sock);

    return 0;
}
