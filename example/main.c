#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "ws_ctube.h"
#include "simulation.h"

int main()
{
	/* create websocket server */
	int port = 9999;
	int max_conn = 2;
	int timeout_ms = 5000;
	double max_broadcast_fps = 24;
	struct ws_ctube *ctube = ws_ctube_open(port, max_conn, timeout_ms, max_broadcast_fps);

	/* run sim */
	pthread_mutex_t *example_data_mutex;
	void *data;
	size_t data_bytes;
	simulation_init(&example_data_mutex);
	for (;;) {
		simulation_step(&data, &data_bytes);

		/* broadcast data */
		pthread_mutex_lock(example_data_mutex);
		ws_ctube_broadcast(ctube, data, data_bytes);
		pthread_mutex_unlock(example_data_mutex);
	}
	simulation_destroy();

	/* cleanup websocket server */
	ws_ctube_close(ctube);

	return 0;
}
