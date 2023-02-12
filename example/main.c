#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ws_ctube.h"
#include "simulation.h"

float dt(struct timespec *t1, struct timespec *t0)
{
	return t1->tv_sec - t0->tv_sec + (float)(t1->tv_nsec - t0->tv_nsec) / 1e9;
}

int main()
{
	/* create websocket server */
	int port = 9999;
	int max_conn = 2;
	int timeout_ms = 5000;
	struct ws_ctube *ctube = ws_ctube_open(port, max_conn, timeout_ms);

	/* broadcast at 24 fps */
	float broadcast_fps = 24;
	struct timespec prev_time, cur_time;

	/* run sim */
	simulation_init();
	clock_gettime(CLOCK_MONOTONIC, &prev_time);
	for (;;) {
		size_t data_bytes;
		void *data = simulation_step(&data_bytes);

		/* only broadcast at given fps */
		clock_gettime(CLOCK_MONOTONIC, &cur_time);
		if (dt(&cur_time, &prev_time) >= 1.0/broadcast_fps) {
			ws_ctube_broadcast(ctube, data, data_bytes);
			prev_time = cur_time;
		}
	}
	simulation_destroy();
	ws_ctube_close(ctube);

	return 0;
}
