/*
 * This module define how we handle a bridge between the web socket and
 * the  bridged TCP server.
 *
 * 1. The client connects
 * 2. The server waits for the client handshake message
 * 3. The server answer a valid handshake message
 * 4. The server connects to the bridged server
 * 5. While the connection is active with the client and the bridged server:
 *    1. If there is a message from the client, extract its content and
 *       send it to the bridged server
 *    2. If there is a message from the bridged server, put it in a valid
 *       web socket message and send it to the client
 */
#ifndef _client_h_
#define _client_h_

#include <stdbool.h>

#include <pthread.h>

#include "net.h"

typedef enum client_status {
    CLIENT_ERROR = -1,
    CLIENT_SUCESS = 0,
} client_status_t;

/*
 * This structure contains all data needed to handle a client during its
 * life.
 */
typedef struct client {
    bool alive;
    socket_t ws_sock;
    socket_t server_sock;
    pthread_t thread;

    const char* bridged_host;
    int bridged_port;
} client_t;

/*
 * Initialize a client using the `sock` socket.
 */
void client_init(client_t* client, socket_t sock, const char* bridged_host,
                 int bridged_port);

/*
 * Start the client thread.
 */
client_status_t client_start(client_t* client);

/*
 * Write an unauthorized message on the client web socket.
 */
void client_send_401(client_t* client);

/*
 * Write an internal server error on the client web socket.
 */
void client_send_500(client_t* client);

/*
 * Handle the client life. If something goes wrong, it is written on stderr,
 * and the client's sockets are closed.
 * When the connection with the client is closed, the client `alive` member
 * become false.
 */
void* client_thread(client_t* client);

/*
 * Close a client sockets. Set its `alive` member at false.
 */
void client_close(client_t* client);

#endif

