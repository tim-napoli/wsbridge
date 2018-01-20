/*
 * Networking related stuff.
 */
#ifndef _net_h_
#define _net_h_

typedef int socket_t;

typedef enum net_status {
    NET_ERROR = -1,
    NET_SUCCESS = 0
} net_status_t;

#define SOCKET_ERROR    (-1)

/*
 * Set the given socket `sock` in non-blocking mode.
 * Returns `NET_SUCESS` in case of success or `NET_ERROR` on failure.
 */
net_status_t socket_set_non_blocking(socket_t sock);

/*
 * Create a new TCP server socket listening on port `port` accepting
 * `max_connections` connections.
 * Returns the socket descriptor on success, or `SOCKET_ERROR` in case of
 * failure.
 */
socket_t socket_create_server_tcp(int port, size_t max_connections);

/*
 * Create a new TCP client to the given `hostname` and `port`.
 * Returns `NET_SUCESS` in case of success or `NET_ERROR` on failure.
 */
socket_t socket_create_client_tcp(const char* hostname, int port);

/*
 * Close the given socket.
 */
void socket_close(socket_t sock);

#endif

