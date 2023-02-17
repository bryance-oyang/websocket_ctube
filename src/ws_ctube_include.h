#ifndef WS_CTUBE_H_INCLUDE
#define WS_CTUBE_H_INCLUDE

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include <stddef.h>
#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#ifdef __cplusplus
namespace ws_ctube {
#endif /* __cplusplus */

struct ws_ctube;

/**
 * ws_ctube_open - create a ws_ctube websocket server. When finished, close with
 * ws_ctube_close()
 *
 * @param port port for websocket server
 * @param max_nclient maximum number of websocket client connections allowed
 * @param timeout_ms timeout (ms) for server starting and websocket handshake
 * or 0 for no timeout
 * @param max_broadcast_fps maximum number of broadcasts per second to rate
 * limit broadcasting or 0 for no limit. For best performance, disable by
 * setting to 0 and manually rate limit broadcasts.
 *
 * @return on success, a struct ws_ctube* is returned; on failure,
 * NULL is returned
 */
struct ws_ctube *ws_ctube_open(int port, int max_nclient, int timeout_ms, double max_broadcast_fps);

/**
 * ws_ctube_close - terminate ws_ctube server and cleanup
 */
void ws_ctube_close(struct ws_ctube *ctube);

/**
 * ws_ctube_broadcast - tries to queue data for sending to all connected
 * websocket clients.
 *
 * If max_broadcast_fps was nonzero when ws_ctube_open was called, this function
 * is rate-limited accordingly and returns failure if called too soon.
 *
 * Data is copied to an internal out-buffer, then this function returns. Actual
 * network operations will be handled internally and opaquely by separate
 * threads.
 *
 * Though non-blocking, system calls performed by this function can potentially
 * take tens of microseconds. Try not to unnecessarily call this function in
 * tight loops.
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

#ifdef __cplusplus
} /* namespace ws_ctube */
#endif /* __cplusplus */

#endif /* WS_CTUBE_H_INCLUDE */
