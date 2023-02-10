#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ws_ctube.h"
#include "crypt.h"

int main()
{
	int port = 9999;
	int max_conn = 2;
	int timeout_ms = 500;

	struct ws_ctube *ctube = ws_ctube_open(port, max_conn, timeout_ms);

	for (int i = 0; i < 20; i++, sleep(1)) {
		void *data = malloc(4096);
		if (data == NULL) {
			continue;
		}
		size_t data_size = snprintf(data, 4096, "hello, world! %d\n", i);
		ws_ctube_broadcast(ctube, data, data_size);
		free(data);
	}

	ws_ctube_close(ctube);
	return 0;
}
