#ifndef WS_CTUBE_H_INTERNAL
#define WS_CTUBE_H_INTERNAL

#ifdef __cplusplus
namespace ws_ctube {
#endif /* __cplusplus */

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

#ifdef __cplusplus
} /* namespace ws_ctube */
#endif /* __cplusplus */

#ifdef __cplusplus
#include <stdexcept>

namespace ws_ctube {

class WS_Ctube {
public:
	struct ws_ctube *ctube;

	WS_Ctube(int port, int max_nclient, int timeout_ms, double max_broadcast_fps) {
		ctube = ws_ctube_open(port, max_nclient, timeout_ms, max_broadcast_fps);
		if (ctube == NULL) {
			throw std::runtime_error("WS_Ctube failed to start");
		}
	}

	~WS_Ctube() {
		ws_ctube_close(ctube);
	}

	int broadcast(const void *data, size_t data_size) {
		return ws_ctube_broadcast(ctube, data, data_size);
	}
};

} /* namespace ws_ctube */
#endif /* __cplusplus */

#endif /* WS_CTUBE_H_INTERNAL */
