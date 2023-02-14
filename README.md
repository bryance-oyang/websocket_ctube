# Websocket Ctube
C/C++ is fast. Browser technology (HTML/JS/CSS) makes things pretty. Why not
combine both?

Websocket Ctube (`ws_ctube`) is a barebones API/library to make it easy for a
running C/C++ program to send data to web browsers in real-time and in a
non-blocking manner.

Just call `ws_ctube_broadcast()` to send data to all connected browsers. The
main C/C++ program thread can continue to run while the network operations are
handled by `ws_ctube`.

## Requirements
* POSIX stuff: `pthreads` and friends (aka not Windows)
* (work in progress, not yet implemented) `openssl` for TLS/SSL

## Demo
Demo requires python http.server module and ports 9736, 9743. Run
```shell
./demo.sh
```
then once the server has started, open a modern :) browser to
`http://localhost:9736/heat_equation.html`

The included demo solves the heat equation PDE in a C program and displays real-time
simulation data in a browser HTML5 canvas.

See `example_heat_equation/main.c` for example usage source code.

## Usage
`ws_ctube` is intended to be used as a statically linked library.

### C API
Include `ws_ctube.h` in your project and use
```C
struct ws_ctube *ws_ctube_open(int port, int max_nclient, int timeout_ms, double
max_broadcast_fps);
void ws_ctube_close(struct ws_ctube *ctube);

int ws_ctube_broadcast(struct ws_ctube *ctube, const void *data, size_t
data_size);
```
`ws_ctube_open()`: create the websocket server.

`ws_ctube_close()`: shutdown the websocket server

`ws_ctube_broadcast()`: send arbitrary data to all websocket clients in a
non-blocking manner.

Tip: if other threads can write to `*data`, get a read-lock to protect `*data`
before broadcasting. The read-lock can be released immediately once this
function returns.


See `ws_ctube.h` for detailed documentation.

### Compiling
1. Compile the websocket ctube library: `ws_ctube.a`
```shell
make
```
2. Include `ws_ctube.h` in your project. Use the C API as desired.
3. Compile your project and link with `ws_ctube.a` and `pthread`. Example:
```shell
gcc -o a.out your_file_1.c your_file_2.c ws_ctube.a -lpthread
```

## Architecture
This section describes the internal workings of `ws_ctube`. This is for
documentation purposes only and users do NOT need to know any of this to be able
to use `ws_ctube`.
