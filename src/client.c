#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "client.h"
#include "ws.h"

void client_init(client_t* client, socket_t sock, const char* bridged_host,
                 int bridged_port)
{
    *client = (client_t){
        .server_sock = SOCKET_ERROR,
        .ws_sock = sock,
        .alive = false,
        .thread = 0,
        .bridged_host = bridged_host,
        .bridged_port = bridged_port
    };
}

client_status_t client_start(client_t* client) {
    if (pthread_create(&client->thread, NULL,
                       (void* (*)(void*))&client_thread,
                       client) < 0)
    {
        fprintf(stderr, "client %p: unable to start thread\n", client);
        client_send_500(client);
        client_close(client);
        return CLIENT_ERROR;
    }
    client->alive = true;
    return CLIENT_SUCESS;
}

void client_send_401(client_t* client) {
    static const char* msg = "HTTP/1.1 401 Unauthorized\r\n"
                             "WWW-Authenticate: Basic "
                             "realm=\"Use a valid message\"\r\n\r\n";
    send(client->ws_sock, msg, strlen(msg), 0);
}

void client_send_500(client_t* client) {
    static const char* msg = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    send(client->ws_sock, msg, strlen(msg), 0);
}

void* client_thread(client_t* client) {
    if (ws_do_handshake(client->ws_sock) != WS_SUCCESS) {
        fprintf(stderr, "rejecting client %p\n", client);
        client_send_401(client);
        goto end;
    }

    // Connect to the server
    client->server_sock = socket_create_client_tcp(client->bridged_host,
                                                   client->bridged_port);
    if (client->server_sock == SOCKET_ERROR) {
        fprintf(stderr, "client %p: unable to connect the bridged server\n",
                client);
        goto end;
    }

    // Set sockets in non-bocking mode for main loop
    if (socket_set_non_blocking(client->ws_sock) == NET_ERROR) {
        fprintf(stderr, "client %p: unable to set non-blocking web socket\n",
                client);
        goto end;
    }

    while (client->alive) {
        char* ws_msg = NULL;
        size_t ws_msg_size = 0;

        ws_status_t ws_recv_status = ws_read_message(client->ws_sock,
                                                     &ws_msg, &ws_msg_size);
        if (ws_recv_status == WS_ERROR) {
            fprintf(stderr, "client %p: cannot read client message\n", client);
            goto end;
        } else
        if (ws_recv_status == WS_SUCCESS) {
            printf("client %p: %s\n", client, ws_msg);
        }

        sleep(1);
    }

  end:
    client_close(client);
    return NULL;

}

void client_close(client_t* client) {
    printf("client %p disconnected\n", client);
    socket_gently_close(client->ws_sock);
    if (client->server_sock != SOCKET_ERROR) {
        socket_gently_close(client->server_sock);
    }
    client->alive = false;
}
