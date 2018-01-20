#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <sha1/sha1.h>
#include <b64/b64.h>

#define MAX_CONNECTIONS  32

const char* broadcast_hostname_g = NULL;
int broadcast_port_g = 0;

/* TCP network ------------------------------------------------------------- */
int set_non_blocking_socket(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

/* Web sockets support ----------------------------------------------------- */

/*
 * Compute the WebSocket accept key function of a RSA key.
 * From section "Server Handshake Response":
 */
const char* ws_compute_accept_key(const char* secret_key) {
    const char* magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char buffer[1024];
    sprintf(buffer, secret_key, magic_string);

    // Get SHA-1 of the buffer
    unsigned char digest[21];
    SHA1_CTX sha;
    SHA1Init(&sha);
    SHA1Update(&sha, (uint8_t*)buffer, strlen(buffer));
    SHA1Final(digest, &sha);
    digest[20] = '\0';

    // Convert to base64:
    return b64_encode(digest, strlen((char*)digest));
}

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
 * Main client thread.
 */
void* client_thread(client_t* client) {
    char ws_recv_buffer[4096];
    int ws_recv_size;

    // First, do the handshake
    ws_recv_size = recv(client->ws_sock,
                        ws_recv_buffer,
                        sizeof(ws_recv_buffer) - 1,
                        0);
    if (ws_recv_size < 0) {
        printf("client %p: unable to receive handshake message\n", client);
        goto end;
    }

    // Connect to the server

    // Set sockets in non-bocking mode for main loop
    if (set_non_blocking_socket(client->ws_sock) < 0) {
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

    int ws_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ws_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(listening_port)
    };

    if (bind(ws_sock,
             (const struct sockaddr*)&ws_addr,
             sizeof(struct sockaddr_in))
        < 0)
    {
        printf("unable to bind listening socket on %d\n", listening_port);
        return 1;
    }

    if (listen(ws_sock, MAX_CONNECTIONS) < 0) {
        printf("unable to listen for %d connections\n",
               MAX_CONNECTIONS);
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
