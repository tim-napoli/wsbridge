#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

#include "net.h"
#include "ws.h"

#define MAX_CONNECTIONS  32

const char* broadcast_hostname_g = NULL;
int broadcast_port_g = 0;

/* client ------------------------------------------------------------------ */
typedef struct client {
    bool alive;
    int ws_sock;
    int server_sock;
    pthread_t thread;
} client_t;

/*
 * Release a client slot to allow other connections to be handled.
 */
void release_client(client_t* client) {
    printf("a client disconnected");
    close(client->ws_sock);
    if (client->server_sock >= 0) {
        close(client->server_sock);
    }
    client->alive = false;
}

/*
 * Send a 401 message to the client.
 */
void client_send_401(client_t* client) {
    static const char* msg = "HTTP/1.1 401 Unauthorized\r\n"
                             "WWW-Authenticate: Basic "
                             "realm=\"Use a valid message\"\r\n\r\n";
    send(client->ws_sock, msg, strlen(msg), 0);
}

/*
 * Main client thread.
 */
void* client_thread(client_t* client) {
    char ws_recv_buffer[4096];
    int ws_recv_size;

    if (ws_do_handshake(client->ws_sock) != WS_SUCCESS) {
        fprintf(stderr, "rejecting client %p\n", client);
        client_send_401(client);
        goto end;
    }

    // Connect to the server

    // Set sockets in non-bocking mode for main loop
    if (socket_set_non_blocking(client->ws_sock) == NET_ERROR) {
        printf("client %p: unable to set non-blocking web socket\n", client);
        goto end;
    }

    while (1) {
        ws_recv_size = recv(client->ws_sock,
                            ws_recv_buffer,
                            sizeof(ws_recv_buffer) - 1,
                            0);
        if (ws_recv_size > 0) {
            printf("client %p: %s\n", client, ws_recv_buffer);
        }

        printf("client %p: looped\n", client);
        sleep(1);
    }

  end:
    release_client(client);
    return NULL;
}

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

    broadcast_hostname_g = argv[2];
    int listening_port;
    if (sscanf(argv[1], "%d", &listening_port) != 1) {
        printf("listening port '%s' is not a valid port format.\n", argv[2]);
        return 1;
    }
    if (sscanf(argv[1], "%d", &broadcast_port_g) != 1) {
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
                goto error;
            }

            client_t client = {
                .server_sock = -1,
                .ws_sock = client_sock,
                .alive = true,
            };
            *client_slot = client;
            if (pthread_create(&client_slot->thread, NULL,
                               (void* (*)(void*))&client_thread,
                               client_slot) < 0)
            {
                printf("client %p: unable to start thread\n", client_slot);
                client_slot->alive = false;
                goto error;
            }

           continue;

          error:
           close(client_sock);
        }
    }

    close(ws_sock);

    return 0;
}
