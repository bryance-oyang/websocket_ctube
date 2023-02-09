#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "ws_ctube.h"

int main()
{
	struct ws_ctube *ctube;
	void *data;
	size_t data_size;

	ctube = ws_ctube_open(9999, 10, 100);

	for (int i = 0; i < 10; i++) {
		data = malloc(4096);
		data_size = snprintf(data, 4096, "hello, world! %d\n", i);
		ws_ctube_broadcast(ctube, data, data_size);
		free(data);
		sleep(1);
	}

	ws_ctube_close(ctube);
	return 0;
}
