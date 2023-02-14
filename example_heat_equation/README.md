# Demo for Websocket Ctube

## Start demo
```
./demo.sh
```

## About
See `example_heat_equation/main.c` for usage.

This is an example use case: heat equation pde is solved in real time with a C
program.

Data is transmitted from the C program to all connected websocket clients in a
non-blocking manner (via separate threads) so the main C code can continue to
run.
