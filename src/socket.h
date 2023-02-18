/**
 * @file
 * @brief basic socket functions
 */

#ifndef WS_CTUBE_SOCKET_H
#define WS_CTUBE_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <stddef.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/**
 * send all characters in buf through a socket
 *
 * @param fd file descriptor
 * @param buf buffer
 * @param buf_size size of buffer in bytes
 *
 * @return 0 on success, -1 otherwise
 */
static inline int ws_ctube_socket_send_all(const int fd, const char *buf, ssize_t buf_size)
{
	while (buf_size > 0) {
		ssize_t nsent = send(fd, buf, buf_size, MSG_NOSIGNAL);
		if (nsent < 1) {
			return -1;
		}
		buf += nsent;
		buf_size -= nsent;
	}

	return 0;
}

/**
 * receive all characters up to buf_size or when delim is encountered
 *
 * @param fd file descriptor
 * @param buf buffer to read into
 * @param buf_size size of buffer (including '\0' if delim is not NULL)
 * @param delim should be a null-terminated string or NULL
 * if delim is NULL, receive up to buf_size bytes
 * if delim is not NULL, receive up to buf_size - 1 bytes or until delim is found
 *
 * @return 0 on success, -1 otherwise
 */
static inline int ws_ctube_socket_recv_all(const int fd, char *const buf, const ssize_t buf_size, const char *delim)
{
	char *remain_buf = buf;
	ssize_t remain_size;

	if (buf_size <= 0) {
		return 0;
	}

	if (delim != NULL) {
		remain_size = buf_size - 1;
		buf[buf_size - 1] = '\0';
	} else {
		remain_size = buf_size;
	}

	while (remain_size > 0) {
		ssize_t nrecv = recv(fd, remain_buf, remain_size, MSG_NOSIGNAL);
		if (nrecv < 1) {
			return -1;
		}
		remain_buf += nrecv;
		remain_size -= nrecv;

		/* look for delim */
		if (delim != NULL && remain_size > 0) {
			*remain_buf = '\0';
			char *dpos = strstr(buf, delim);
			if (dpos == NULL) {
				continue;
			}

			dpos += strlen(delim);
			*dpos = '\0';
			return 0;
		}
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

#endif /* WS_CTUBE_SOCKET_H */
