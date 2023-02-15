# Websocket Ctube
Websocket Ctube (`ws_ctube`) is a barebones, header-only library written in C
to make it simple for a running C/C++ program to broadcast data to web browsers
in real-time and in a non-blocking manner.

Call `ws_ctube_broadcast()` to send data to all connected browsers. The main
C/C++ program thread can continue to run while the network operations are
handled by `ws_ctube` in separate threads.

Simply include `ws_ctube.h` in your project and compile with `-pthread`.

## Requirements
* gcc >= 4.7.0 or similar
* POSIX stuff: `pthread` and friends (aka sorry Windows)
* (work in progress, not yet implemented) `openssl` for TLS/SSL

# Example use case
The included demo solves the heat equation PDE in a C program and displays
real-time simulation data in a browser HTML5 canvas.

Demo requires python http.server module and ports 9736, 9743. Run
```shell
./demo.sh
```
then once the server has started, open a modern :) browser to
`http://localhost:9736/heat_equation.html`

See `main.c`, `heat_equation.html` in `example_heat_equation/` for example
source code.

# Usage
`ws_ctube` is most easily used as a header only library. Include `ws_ctube.h`
in your project. For C++, the API is under the namespace `ws_ctube::`.

Alternatively, you can compile and use as a statically linked library by running
`make` in `src/` to generate `ws_ctube.a`

## C++ API
The C++ API provides a RAII wrapper class around the C API described below.

Create and start the `ws_ctube` server

```C++
ws_ctube::WS_Ctube ctube{port, max_nclient, timeout_ms,
max_broadcast_fps};
```

Non-blocking broadcast to connected browsers
```C++
ctube.broadcast(data, data_size);
```

## C API
**Note:** in C++, the C API is namespaced into `ws_ctube::`.

```C
struct ws_ctube *ctube = ws_ctube_open(port, max_nclient, timeout_ms,
max_broadcast_fps);
/* do stuff */
ws_ctube_broadcast(ctube, data, data_size);
/* do more stuff */
ws_ctube_close(ctube);
```

### Details
```C
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
```

# Internal Architecture
WIP: This section describes the internal workings of `ws_ctube`. This is for
documentation purposes only and is not needed to use the API.
