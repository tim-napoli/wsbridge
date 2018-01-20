#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

#include <sha1/sha1.h>
#include <b64/b64.h>

#include "net.h"

#define MAX_CONNECTIONS  32

const char* broadcast_hostname_g = NULL;
int broadcast_port_g = 0;

/* Web sockets support ----------------------------------------------------- */

int ws_handshake_req_get_key(const char* msg, size_t len, char* out_key) {
    const char* WEB_KEY_STR = "Sec-WebSocket-Key: ";

    for (size_t i = 0; i < len; i++) {
        if (strncmp(msg + i, WEB_KEY_STR, strlen(WEB_KEY_STR)) == 0) {
            i += strlen(WEB_KEY_STR);
            size_t key_idx = 0;
            while (msg[i] != '\r') {
                out_key[key_idx++] = msg[i++];
            }
            out_key[key_idx] = '\0';
            return 0;
        }
    }

    return -1;
}

/*
 * Compute the WebSocket accept key function of a RSA key.
 * From section "Server Handshake Response":
 */
const char* ws_compute_accept_key(const char* secret_key) {
    const char* magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char buffer[1024];
    sprintf(buffer, "%s%s", secret_key, magic_string);

    // Get SHA-1 of the buffer
    unsigned char digest[20];
    SHA1_CTX sha;
    SHA1Init(&sha);
    SHA1Update(&sha, (uint8_t*)buffer, strlen(buffer));
    SHA1Final(digest, &sha);

    // Convert to base64:
    return b64_encode(digest, 20);
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
    char ws_write_buffer[4096];
    int ws_write_size;

    // First, do the handshake
    ws_recv_size = recv(client->ws_sock,
                        ws_recv_buffer,
                        sizeof(ws_recv_buffer) - 1,
                        0);
    if (ws_recv_size < 0) {
        printf("client %p: unable to receive handshake message\n", client);
        goto end;
    }

    // Generate key
    char key_buf[512];
    if (ws_handshake_req_get_key(ws_recv_buffer, ws_recv_size, key_buf)) {
        printf("client %p: unable to get WebSocket ky\n", client);
        goto end;
    }
    printf("Key is %s\n", key_buf);
    const char* access_key = ws_compute_accept_key(key_buf);
    printf("Access key is %s\n", access_key);

    // Send handshake answer
    sprintf(ws_write_buffer,
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n",
            access_key);
    ws_write_size = write(client->ws_sock,
                          ws_write_buffer,
                          strlen(ws_write_buffer));
    if (ws_write_size < 0) {
        printf("client %p: unable to write handshake message\n", client);
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
