/**
 * @file
 * @brief basic socket functions
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <stddef.h>

static inline int ws_ctube_send_all(int fd, char *buf, size_t len)
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

static inline int ws_ctube_recv_all(int fd, char *buf, size_t buf_size, char *delim)
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

static inline int ws_ctube_bind_server(int server_sock, int port)
{
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(server_sock, (struct sockaddr *)&sa, sizeof(sa));
}

#endif /* SOCKET_H */
