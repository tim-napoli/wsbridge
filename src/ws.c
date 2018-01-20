#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <sha1/sha1.h>
#include <b64/b64.h>

#include "ws.h"

ws_status_t ws_client_handshake_get_key(const char* msg, char* out_key) {
    static const char* WEB_KEY_STR = "Sec-WebSocket-Key: ";
    for (size_t i = 0; msg[i] != '\0'; i++) {
        if (strncmp(msg + i, WEB_KEY_STR, strlen(WEB_KEY_STR)) == 0) {
            i += strlen(WEB_KEY_STR);
            size_t key_idx = 0;
            while (msg[i] != '\r') {
                out_key[key_idx++] = msg[i++];
            }
            out_key[key_idx] = '\0';
            return WS_SUCCESS;
        }
    }

    return WS_ERROR;
}

const char* ws_compute_accept_key(const char* secret_key) {
    const char* MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char buffer[1024];
    sprintf(buffer, "%s%s", secret_key, MAGIC_STRING);

    // Get SHA-1 of the buffer
    unsigned char digest[20];
    SHA1_CTX sha;
    SHA1Init(&sha);
    SHA1Update(&sha, (uint8_t*)buffer, strlen(buffer));
    SHA1Final(digest, &sha);

    // Convert to base64:
    return b64_encode(digest, 20);
}

ws_status_t ws_do_handshake(socket_t ws_sock) {
    int recv_size;
    char recv_buf[4096];
    int write_size;
    char write_buf[4096];

    // Waits for the client handshake message
    recv_size = recv(ws_sock, recv_buf, sizeof(recv_buf) - 1, 0);
    if (recv_size < 0) {
        fprintf(stderr, "unable to receive handshake message\n");
        return WS_ERROR;
    }
    recv_buf[recv_size] = '\0';

    // Generate key
    char key_buf[512];
    if (ws_client_handshake_get_key(recv_buf, key_buf) == WS_ERROR)
    {
        fprintf(stderr, "unable to find the client handshake key\n");
        return WS_ERROR;
    }
    const char* access_key = ws_compute_accept_key(key_buf);

    // Send handshake answer
    sprintf(write_buf,
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n",
            access_key);
    write_size = send(ws_sock, write_buf, strlen(write_buf), 0);
    if (write_size < 0) {
        fprintf(stderr, "unable to send server handshake message\n");
        return WS_ERROR;
    }

    return WS_SUCCESS;
}
