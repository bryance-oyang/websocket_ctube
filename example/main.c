#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ws_ctube.h"
#include "simulation.h"

int main()
{
	int port = 9999;
	int max_conn = 2;
	int timeout_ms = 500;

	struct ws_ctube *ctube = ws_ctube_open(port, max_conn, timeout_ms);
	simulation_init();
	for (;;) {
		void *data = simulation_step();
		size_t data_bytes = GRID_SIDE*GRID_SIDE;
		ws_ctube_broadcast(ctube, data, data_bytes);
	}
	simulation_destroy();
	ws_ctube_close(ctube);

	return 0;
}
