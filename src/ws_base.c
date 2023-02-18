/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file
 * @brief basic websocket functions
 */

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "socket.h"
#include "ws_base.h"
#include "crypt.h"

#define WS_DEBUG 0
#define WS_BUFLEN 4096

static void ws_print_frame(const char *prefix, char *frame, int len)
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

/** create data frame according to websocket standard */
int ws_ctube_ws_mkframe(char *frame, const char *msg, size_t msg_size, int first)
{
	int payld_size;

	first = !!first;
	if (msg_size > WS_CTUBE_MAX_PAYLD_SIZE) {
		frame[0] = 2*first;
		payld_size = WS_CTUBE_MAX_PAYLD_SIZE;
	} else {
		frame[0] = 0b10000000 + 2*first;
		payld_size = msg_size;
	}

	frame[1] = payld_size;
	memcpy(&frame[WS_CTUBE_FRAME_HDR_SIZE], msg, payld_size);

	return payld_size;
}

/** send data in data frames according to websocket standard */
int ws_ctube_ws_send(int conn, const char *msg, size_t msg_size)
{
	int payld_size;
	char frame[WS_BUFLEN];

	for (int first = 1; msg_size > 0; first = 0, msg += payld_size, msg_size -= payld_size) {
		payld_size = ws_ctube_ws_mkframe(frame, msg, msg_size, first);
		const int frame_len = payld_size + WS_CTUBE_FRAME_HDR_SIZE;
		ws_print_frame("ws_ctube_ws_send()", frame, frame_len);
		if (ws_ctube_socket_send_all(conn, frame, frame_len) != 0) {
			return -1;
		}
	}

	return 0;
}

int ws_ctube_ws_recv(int conn, char *msg, int *msg_size, size_t max_msg_size)
{
	/* TODO */
	(void)conn;
	(void)msg;
	(void)msg_size;
	(void)max_msg_size;
	return -1;
}

int ws_ctube_ws_is_ping(const char *msg, int msg_size)
{
	/* TODO */
	(void)msg;
	(void)msg_size;
	return 0;
}

int ws_ctube_ws_pong(int conn, const char *msg, int msg_size)
{
	/* TODO */
	(void)conn;
	(void)msg;
	(void)msg_size;
	return 0;
}

/** extract the client key from handshake */
static char *ws_client_key(char *rbuf)
{
	char *client_key, *client_key_end;

	client_key = strstr(rbuf, "Sec-WebSocket-Key: ");
	if (client_key == NULL) {
		return NULL;
	}

	client_key += strlen("Sec-WebSocket-Key: ");
	client_key_end = strstr(client_key, "\r");
	if (client_key_end == NULL) {
		return NULL;
	}

	*client_key_end = '\0';
	if (WS_DEBUG) {
		printf("wskey\n%s\n", client_key);
	}
	return client_key;
}

/** compute server response key per websocket standard */
static int ws_server_response_key(char *server_key, const char *client_key)
{
	size_t sha1_hash_len = 20;
	char magic_client_key[WS_BUFLEN];
	char client_sha1[WS_BUFLEN];

	if (snprintf(magic_client_key, WS_BUFLEN, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", client_key) >= WS_BUFLEN) {
		return -1;
	}

	ws_ctube_sha1sum((unsigned char *)client_sha1, (unsigned char *)magic_client_key, strlen(magic_client_key));
	ws_ctube_b64_encode((unsigned char *)server_key, (unsigned char *)client_sha1, sha1_hash_len);

	return 0;
}

int ws_ctube_ws_handshake(int conn, const struct timeval *timeout)
{
	char rbuf[WS_BUFLEN];
	char *client_key;
	char server_key[WS_BUFLEN];
	char response[2*WS_BUFLEN];

	const char *const response_fmt = "HTTP/1.1 101 Switching Protocols\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Accept: %s\r\n\r\n";

	/* receive with timeout, but reset to old timeout afterwards */
	struct timeval old_timeout;
	socklen_t timeval_size = sizeof(old_timeout);
	if (getsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &old_timeout, &timeval_size) < 0) {
		goto err;
	}
	if (setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(*timeout)) < 0) {
		goto err;
	}
	if (ws_ctube_socket_recv_all(conn, rbuf, WS_BUFLEN, "\r\n\r\n") != 0) {
		goto err;
	}
	if (setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &old_timeout, sizeof(old_timeout)) < 0) {
		goto err;
	}

	/* ensure null termination of received data */
	rbuf[WS_BUFLEN - 1] = '\0';

	if (WS_DEBUG) {
		printf("get\n%s\n", rbuf);
	}

	client_key = ws_client_key(rbuf);
	if (client_key == NULL) {
		goto err;
	}
	if (ws_server_response_key(server_key, client_key) != 0) {
		goto err;
	}

	snprintf(response, sizeof(response)/sizeof(response[0]), response_fmt, server_key);
	if (WS_DEBUG) {
		printf("server response\n%s\n", response);
	}

	/* send with timeout, but reset to old timeout afterwards */
	if (getsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &old_timeout, &timeval_size) < 0) {
		goto err;
	}
	if (setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, timeout, sizeof(*timeout)) < 0) {
		goto err;
	}
	if (ws_ctube_socket_send_all(conn, response, strlen(response)) != 0) {
		goto err;
	}
	if (setsockopt(conn, SOL_SOCKET, SO_SNDTIMEO, &old_timeout, sizeof(old_timeout)) < 0) {
		goto err;
	}

	return 0;

err:
	if (WS_DEBUG) {
		perror("ws_ctube_ws_handshake()");
	}
	return -1;
}
