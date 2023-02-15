#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "ws_ctube.h"
#include "simulation.h"

#ifdef __cplusplus
using namespace ws_ctube;
#endif

int main()
{
	/* ws_ctube server parameters */
	int port = 9743;
	int max_nclient = 100;
	int timeout_ms = 0; // disable timeout
	double max_broadcast_fps = 24;

	/* create websocket ctube server */
	printf("starting websocket ctube...\n");
	fflush(stdout);
	struct ws_ctube *ctube = ws_ctube_open(port, max_nclient, timeout_ms, max_broadcast_fps);
	if (ctube == NULL) {
		fprintf(stderr, "websocket ctube failed to start\n");
		fflush(stderr);
		return -1;
	}
	printf("websocket ctube started :D\n\n");
	fflush(stdout);

	/* start example simulation */
	pthread_mutex_t *example_data_mutex;
	void *data;
	size_t data_bytes;
	if (simulation_init(&example_data_mutex) != 0) {
		fprintf(stderr, "demo simulation failed to init\n");
		fflush(stderr);
		return -1;
	}

	/* example simulation main loop */
	for (;;) {
		simulation_step(&data, &data_bytes);

		/* send data to connected clients via websocket ctube */
		pthread_mutex_lock(example_data_mutex);
		ws_ctube_broadcast(ctube, data, data_bytes);
		pthread_mutex_unlock(example_data_mutex);
	}

	/* shutdown/cleanup example */
	simulation_destroy();
	ws_ctube_close(ctube);
	return 0;
}
