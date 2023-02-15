/**
 * @file
 * @brief The main include file for ws_ctube
 */

#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>
#include <stddef.h>

struct ws_ctube;

/**
 * Create a ws_ctube websocket server that must be closed with ws_ctube_close()
 *
 * @param port port for websocket server
 * @param max_nclient maximum number of websocket client connections allowed
 * @param timeout_ms timeout (ms) for server starting and websocket handshake
 * or 0 for no timeout
 * @param max_broadcast_fps maximum number of broadcasts per second to rate
 * limit broadcasting or 0 for no limit
 *
 * @return on success, a struct ws_ctube* is returned; on failure,
 * NULL is returned
 */
struct ws_ctube *ws_ctube_open(int port, int max_nclient, int timeout_ms, double max_broadcast_fps);

/**
 * Terminate ws_ctube server and cleanup
 */
void ws_ctube_close(struct ws_ctube *ctube);

/**
 * Tries to queue data for sending to all connected websocket clients. Data is
 * copied to an internal out-buffer, then this function returns. Actual network
 * operations are handled internally by separate threads.
 *
 * If other threads can write to *data, get a read-lock to protect *data before
 * broadcasting. The read-lock can be released immediately once this function
 * returns.
 *
 * @param ctube the websocket ctube
 * @param data pointer to data to broadcast
 * @param data_size bytes of data
 *
 * @return 0 on success, nonzero otherwise
 */
int ws_ctube_broadcast(struct ws_ctube *ctube, const void *data, size_t data_size);

#endif /* WS_CTUBE_H */
