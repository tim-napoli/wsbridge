#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <byteswap.h>

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

/*                           FRAME FORMAT
 *
 *  0               1               2               3
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 * |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 * |     Extended payload length continued, if payload len == 127  |
 * + - - - - - - - - - - - - - - - +-------------------------------+
 * |                               |Masking-key, if MASK set to 1  |
 * +-------------------------------+-------------------------------+
 * | Masking-key (continued)       |          Payload Data         |
 * +-------------------------------- - - - - - - - - - - - - - - - +
 * :                     Payload Data continued ...                :
 * + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 * |                     Payload Data continued ...                |
 * +---------------------------------------------------------------
 */

/*
 * NOTE The structures bytes are mapped on a big endian fashion.
 */
typedef struct __ws_frame_head {
    uint8_t opcode : 4;
    uint8_t rsv : 3;
    uint8_t fin : 1;

    uint8_t payload : 7;
    uint8_t mask : 1;
} __ws_frame_head_t;

#define RECV(what) \
    recv_size = recv(ws_sock, &what, sizeof(what), 0); \
    if (recv_size != sizeof(what)) { \
        fprintf(stderr, "expecting %zu bytes, read %d\n", \
                sizeof(what), recv_size); \
        return WS_ERROR; \
    }

ws_status_t __ws_read_message_content(socket_t ws_sock,
                                      __ws_frame_head_t frame_head,
                                      char** output,
                                      size_t* output_size)
{
    int recv_size;

    // Get the payload len
    uint64_t payload_len = frame_head.payload;
    if (payload_len == 126) {
        uint16_t payload_next;
        RECV(payload_next);
        payload_len = __bswap_16(payload_next);
    } else
    if (payload_len == 127) {
        RECV(payload_len);
        payload_len = __bswap_64(payload_len);
        if (payload_len & ((uint64_t)1) << 63) {
            return WS_ERROR;
        }
    }

    // Get the mask key if any.
    uint32_t mask_key;
    if (frame_head.mask) {
        RECV(mask_key);
    }

    // Read the data
    *output_size = payload_len;
    *output = malloc(payload_len + 1);
    recv_size = recv(ws_sock, *output, payload_len, 0);
    if (recv_size != payload_len) {
        fprintf(stderr, "expecting %zu bytes, read %d\n",
                payload_len, recv_size);
        free(*output);
        return WS_ERROR;
    }

    // Decode the data.
    if (frame_head.mask) {
        for (size_t i = 0; i < payload_len; i++) {
            (*output)[i] ^= (char)(mask_key >> ((i % 4) * 8));
        }
    }

    (*output)[payload_len] = '\0';

    return WS_SUCCESS;
}

ws_status_t ws_read_message(socket_t ws_sock,
                            ws_opcode_t* opcode,
                            char** output,
                            size_t* output_size)
{
    int recv_size;
    __ws_frame_head_t frame_head;

    // First, try to read the first part of the message:
    recv_size = recv(ws_sock, &frame_head, sizeof(frame_head), 0);
    if (recv_size < 0) {
        return WS_NOTHING;
    } else
    if (recv_size != sizeof(frame_head)) {
        fprintf(stderr, "cannot read frame header\n");
        return WS_ERROR;
    }

    if (!frame_head.fin) {
        fprintf(stderr, "unsupported multi-frame messages\n");
        return WS_ERROR;
    }

    *opcode = frame_head.opcode;
    switch (frame_head.opcode) {
        case WS_OP_TEXT_FRAME:
        case WS_OP_BINARY_FRAME:
          return __ws_read_message_content(ws_sock, frame_head, output,
                                           output_size);

        case WS_OP_PING:
        case WS_OP_PONG:
        case WS_OP_CLOSE:
          socket_flush(ws_sock);
          return WS_SUCCESS;

        case WS_OP_CONTINUATION_FRAME:
        default:
          fprintf(stderr, "unsupported opcode %x\n", *opcode);
          return WS_ERROR;
    }


    return WS_SUCCESS;
}

ws_status_t ws_send_message(socket_t ws_sock, ws_opcode_t op,
                            const char* msg, size_t msg_size)
{
    __ws_frame_head_t frame_head = {
        .fin = 1,
        .rsv = 0,
        .opcode = WS_OP_TEXT_FRAME,
        .mask = 0,
        .payload = (msg_size < 126) ? msg_size
                 : (msg_size < sizeof(uint16_t)) ? 126
                 : 127
    };

#define SEND(what) \
    if (send(ws_sock, &what, sizeof(what), 0) != sizeof(what)) { \
        fprintf(stderr, "unable to send message to client\n"); \
        return WS_ERROR; \
    }

    SEND(frame_head);

    if (msg_size && msg) {
        if (msg_size > 125 && msg_size < sizeof(uint16_t)) {
            uint16_t msg_size_u16 = __bswap_16((uint16_t)msg_size);
            SEND(msg_size_u16);
        } else if (msg_size > 125) {
            uint64_t msg_size_u64 = __bswap_64((uint64_t)msg_size);
            SEND(msg_size_u64);
        }

        if (send(ws_sock, msg, msg_size, 0) != msg_size) {
            fprintf(stderr, "unable to send message content to client\n");
            return WS_ERROR;
        }
    }

    return WS_SUCCESS;
}
