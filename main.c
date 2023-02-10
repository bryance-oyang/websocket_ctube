#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ws_ctube.h"
#include "crypt.h"

int main()
{
	char in[4096];
	char out[4096];
	char enc[4096];

	//snprintf(in, 4096, "The quick brown fox jumps over the lazy dog");
	in[0] = '\0';
	//in[0] = 0b01100001;
	//in[1] = 0b01100010;
	//in[2] = 0b01100011;
	//in[3] = 0b01100100;
	//in[4] = 0b01100101;
	//in[5] = 0;

	sha1sum((unsigned char *)out, (unsigned char *)in, strlen(in));
	b64_encode((unsigned char *)enc, (unsigned char *)out, 20);
	printf("%s\n", enc);
	return 0;

	struct ws_ctube *ctube;
	void *data;
	size_t data_size;

	ctube = ws_ctube_open(9999, 2, 500);

	for (int i = 0; i < 20; i++) {
		data = malloc(4096);
		data_size = snprintf(data, 4096, "hello, world! %d\n", i);
		ws_ctube_broadcast(ctube, data, data_size);
		free(data);
		sleep(1);
	}

	ws_ctube_close(ctube);
	return 0;
}
