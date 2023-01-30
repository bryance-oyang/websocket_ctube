#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "ws_ctube.h"

int main()
{
	struct ws_ctube ctube;
	ws_ctube_init(&ctube, 9999, 10);

	for (int i = 0; i < 20; i++) {
		ctube.data = malloc(4096);
		ctube.data_size = snprintf(ctube.data, 4096, "hello, world! %d\n", i);
		ws_ctube_broadcast(&ctube);
		free(ctube.data);
		sleep(1);
	}

	ws_ctube_destroy(&ctube);
	return 0;
}
