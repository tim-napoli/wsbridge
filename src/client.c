#include <stdio.h>
#include <stdlib.h>
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
    return CLIENT_SUCCESS;
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

static client_status_t _client_handle_ws(client_t* client) {
    char* ws_msg = NULL;
    size_t ws_msg_size = 0;
    ws_opcode_t ws_opc = 0;

    ws_status_t ws_recv_status = ws_read_message(client->ws_sock, &ws_opc,
                                                 &ws_msg, &ws_msg_size);
    if (ws_recv_status == WS_ERROR) {
        fprintf(stderr, "client %p: cannot read client message\n", client);
        goto error_end;
    } else
    if (ws_recv_status == WS_NOTHING) {
        goto end;
    }

    printf("client %p: (%x) %s\n", client, ws_opc, ws_msg);
    switch (ws_opc) {
      case WS_OP_CLOSE:
        client->alive = false;
        break;

      case WS_OP_PING:
        if (ws_send_message(client->ws_sock, WS_OP_PONG, NULL, 0)
            != WS_SUCCESS)
        {
            fprintf(stderr, "client %p: cannot send pong\n", client);
            goto error_free;
        }
        break;

      case WS_OP_TEXT_FRAME:
      case WS_OP_BINARY_FRAME:
        if (ws_send_message(client->ws_sock, WS_OP_TEXT_FRAME,
                            ws_msg, ws_msg_size)
            != WS_SUCCESS)
        {
            fprintf(stderr, "client %p: cannot send message\n", client);
            goto error_free;
        }

      default:
        break;
    }

  end:
    if (ws_msg) {
        free(ws_msg);
    }
    return CLIENT_SUCCESS;

  error_free:
    if (ws_msg) {
        free(ws_msg);
    }
  error_end:
    return CLIENT_ERROR;
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
        if (_client_handle_ws(client) == CLIENT_ERROR) {
            goto end;
        }

        sleep(1);
    }

  end:
    client_close(client);
    return NULL;

}

void client_close(client_t* client) {
    printf("client %p disconnected\n", client);
    ws_send_message(client->ws_sock, WS_OP_CLOSE, NULL, 0);
    socket_gently_close(client->ws_sock);
    if (client->server_sock != SOCKET_ERROR) {
        socket_gently_close(client->server_sock);
    }
    client->alive = false;
}
