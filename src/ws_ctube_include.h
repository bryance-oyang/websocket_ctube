#ifndef WS_CTUBE_H_INCLUDE
#define WS_CTUBE_H_INCLUDE

#ifndef __cplusplus
#include <stddef.h>
#else /* __cplusplus*/
#include <cstddef>
#endif /* __cplusplus */

#ifdef __cplusplus
namespace ws_ctube {
#endif /* __cplusplus */

struct ws_ctube;

/**
 * ws_ctube_open - create a ws_ctube websocket server that must be closed with
 * ws_ctube_close()
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
 * Though non-blocking, system calls performed by this function can possibly
 * take tens of microseconds. Try not to unnecessarily call this function.
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

#ifdef __cplusplus
#include <stdexcept>

namespace ws_ctube {

class WS_Ctube {
public:
	struct ws_ctube *_ctube;

	WS_Ctube(int port, int max_nclient, int timeout_ms, double max_broadcast_fps) {
		_ctube = ws_ctube_open(port, max_nclient, timeout_ms, max_broadcast_fps);
		if (_ctube == NULL) {
			throw ::std::runtime_error("WS_Ctube failed to start");
		}
	}

	~WS_Ctube() {
		ws_ctube_close(_ctube);
	}

	int broadcast(const void *data, size_t data_size) {
		return ws_ctube_broadcast(_ctube, data, data_size);
	}
};

} /* namespace ws_ctube */
#endif /* __cplusplus */

#endif /* WS_CTUBE_H_INCLUDE */
