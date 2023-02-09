/** @file
 * @brief basic socket functions
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <stddef.h>
#include <netinet/in.h>
#include <sys/socket.h>

static inline int send_all(int fd, char *buf, size_t len)
{
	while (len > 0) {
		int nsent = send(fd, buf, len, MSG_NOSIGNAL);
		if (nsent < 1) {
			return -1;
		}
		buf += nsent;
		len -= nsent;
	}
	return 0;
}

static inline int recv_all(int fd, char *buf, size_t buf_size, char *delim)
{
	while (buf_size > 0) {
		int nrecv = recv(fd, buf, buf_size, MSG_NOSIGNAL);
		if (nrecv < 1) {
			return -1;
		}
		if (delim != NULL && strstr(buf, delim) != NULL) {
			return 0;
		}
		buf += nrecv;
		buf_size -= nrecv;
	}
	return 0;
}

static inline int bind_server(int server_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(server_sock, (struct sockaddr *)&sa, sizeof(sa));
}

#endif /* SOCKET_H */
