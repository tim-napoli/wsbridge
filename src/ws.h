#ifndef _ws_h_
#define _ws_h_

#include "net.h"

typedef enum ws_status {
    WS_ERROR = -1,
    WS_SUCCESS = 0,
    WS_NOTHING = 1,
} ws_status_t;

typedef enum ws_opcode {
    WS_OP_CONTINUATION_FRAME = 0x0,
    WS_OP_TEXT_FRAME         = 0x1,
    WS_OP_BINARY_FRAME       = 0x2,
    WS_OP_CLOSE              = 0x8,
    WS_OP_PING               = 0x9,
    WS_OP_PONG               = 0xa,
} ws_opcode_t;

/*
 * Fill `out_key` with the "Sec-WebSocket-Key" section of the request content
 * string `msg`.
 * Returns `WS_ERROR` if the key cannot be found or `WS_SUCCESS` otherwise.
 */
ws_status_t ws_client_handshake_get_key(const char* msg, char* out_key);

/*
 * Compute the WebSocket accept key of the handshake message key.
 */
const char* ws_compute_accept_key(const char* secret_key);

/*
 * Wait for the handshake message from client, check its content and answer
 * a valid server handshake message.
 * If something goes wrong, returns `WS_ERROR`, otherwise returns `WS_SUCCESS`.
 */
ws_status_t ws_do_handshake(socket_t ws_sock);

/*
 * Read the next message pending on `ws_sock`. Allocates *output with the
 * decoded message content, and set *output_len to the length of the output.
 * `opcode` will be set to the received frame opcode.
 * Returns `WS_ERROR` if an error occured, `WS_NOTHING` if there was noting to
 * read or `WS_SUCCESS` if a message has been read.
 *
 * XXX This function doesn't support messages sent in multiple frames for now.
 */
ws_status_t ws_read_message(socket_t ws_sock,
                            ws_opcode_t* opcode,
                            char** output,
                            size_t* output_len);

/*
 * Send the given message content through `ws_sock`.
 * Returns `WS_ERROR` on failure, or `WS_SUCCESS` otherwise.
 */
ws_status_t ws_send_message(socket_t ws_sock, ws_opcode_t op,
                            const char* msg,
                            size_t msg_size);

#endif
