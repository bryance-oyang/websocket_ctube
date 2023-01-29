#include <unistd.h>

#include "ws_ctube.h"

int main()
{
	struct ws_ctube ctube;
	ws_ctube_init(&ctube, 9999);

	sleep(10);

	ws_ctube_destroy(&ctube);
	return 0;
}
