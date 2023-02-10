/** @file
 * @brief The main include file for ws_ctube
 */

#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

/** defined in ws_ctube_struct.h */
struct ws_ctube;

/**
 * create a ws_ctube that must be closed with ws_ctube_close()
 *
 * @param port port for websocket server
 * @param conn_limit maximum number of connections allowed
 * @param timeout_ms timeout (ms) for socket operations and server starting
 */
struct ws_ctube *ws_ctube_open(int port, int conn_limit, int timeout_ms);

/**
 * terminate websocket server and cleanup
 */
void ws_ctube_close(struct ws_ctube *ctube);

/**
 * try to send data to all connected websocket clients
 *
 * @param ctube the websocket ctube
 * @param data data to broadcast
 * @param data_size bytes of data
 */
int ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size);

#endif /* WS_CTUBE_H */
