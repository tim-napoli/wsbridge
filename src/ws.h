#ifndef _ws_h_
#define _ws_h_

#include "net.h"

typedef enum ws_status {
    WS_ERROR = -1,
    WS_SUCCESS = 0,
} ws_status_t;

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

#endif
