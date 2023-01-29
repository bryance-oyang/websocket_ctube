#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "crypt.h"

#define WS_DEBUG 1
#define WS_BUFLEN 4096

static int send_all(int fd, char *buf, size_t len)
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

static int recv_all(int fd, char *buf, size_t buf_size, char *delim)
{
	while (buf_size > 0) {
		int nrecv = recv(fd, buf, buf_size, 0);
		if (nrecv < 0) {
			return -1;
		}
		if (strstr(buf, delim) != NULL)
			return 0;
		buf += nrecv;
		buf_size -= nrecv;
	}
	return 0;
}

static void ws_print_frame(char *prefix, char *frame, int len)
{
	if (!WS_DEBUG) {
		return;
	}

	printf("%s\n", prefix);
	for (int i = 0; i < len; i++) {
		for (int j = 7; j >= 0; j--) {
			uint8_t mask = ((uint8_t)1 << j);
			uint8_t val = ((frame[i] & mask) >> j);
			printf("%u", val);
		}
		printf("|");
		if (i % 4 == 3) {
			printf("\n");
		}
	}
	printf("\n\n");
	fflush(stdout);
}

int ws_send(int conn, const char *msg, int msg_size)
{
	int payld_size;
	const int payld_offset = 2;
	int frame_len;
	char frame[WS_BUFLEN];

	for (int first = 1; msg_size > 0;
	     msg_size -= payld_size, msg += payld_size, first = 0) {
		memset(frame, 0, 128);

		if (msg_size > 125) {
			frame[0] = first;
			payld_size = 125;
		} else {
			frame[0] = 0b10000000 + first;
			payld_size = msg_size;
		}
		frame[1] = payld_size;

		frame_len = payld_offset + payld_size;
		memcpy(&frame[payld_offset], msg, payld_size);
		ws_print_frame("ws_send", frame, frame_len);

		if (send_all(conn, frame, frame_len) != 0)
			return -1;
	}

	return 0;
}

int ws_recv(int conn, char *msg, int *msg_size, int max_msg_size)
{
	(void)conn;
	(void)msg;
	(void)msg_size;
	(void)max_msg_size;
	return 0;
}

int ws_is_ping(const char *msg, int msg_size)
{
	(void)msg;
	(void)msg_size;
	return 0;
}

int ws_pong(int conn, const char *msg, int msg_size)
{
	(void)conn;
	(void)msg;
	(void)msg_size;
	return 0;
}

static char *ws_client_key(char *rbuf)
{
	char *client_key, *client_key_end;

	client_key = strstr(rbuf, "Sec-WebSocket-Key: ");
	client_key += strlen("Sec-WebSocket-Key: ");
	client_key_end = strstr(client_key, "\r");
	*client_key_end = '\0';
	if (WS_DEBUG) {
		printf("wskey\n%s\n", client_key);
	}
	return client_key;
}

static int ws_server_response_key(char *server_key, const char *client_key)
{
	size_t sha1_hash_len = 20;
	char magic_client_key[WS_BUFLEN];
	char client_sha1[WS_BUFLEN];

	strncpy(magic_client_key, client_key, WS_BUFLEN);
	strncat(magic_client_key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
		WS_BUFLEN - strlen(magic_client_key));

	sha1sum((unsigned char *)client_sha1, (unsigned char *)magic_client_key, strlen(magic_client_key));
	b64_encode((unsigned char *)server_key, (unsigned char *)client_sha1, sha1_hash_len);

	return 0;
}

int ws_handshake(int conn)
{
	char rbuf[WS_BUFLEN];
	char *client_key;
	char server_key[WS_BUFLEN];
	char response[WS_BUFLEN];

	recv_all(conn, rbuf, WS_BUFLEN, "\r\n\r\n");
	if (WS_DEBUG) {
		printf("get\n%s\n", rbuf);
	}

	client_key = ws_client_key(rbuf);
	ws_server_response_key(server_key, client_key);

	const char *const response_fmt = "HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n\r\n";
	snprintf(response, WS_BUFLEN - strlen(response_fmt), response_fmt,
		 server_key);
	if (WS_DEBUG) {
		printf("server response\n%s\n", response);
	}
	send_all(conn, response, strlen(response));

	return 0;
}
