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

struct ws_thread_data {
	int conn;
	pthread_mutex_t *print_mutex;

	char *data;
	int *data_ready;
	pthread_mutex_t *data_mutex;
	pthread_cond_t *data_ready_cond;
};

static int send_all(int fd, char *buf, size_t len)
{
	while (len > 0) {
		int nsent = send(fd, buf, len, 0);
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

static void ws_print_frame(char *prefix, char *frame, int len,
		 pthread_mutex_t *print_mutex)
{
	if (!WS_DEBUG) {
		return;
	}

	pthread_mutex_lock(print_mutex);
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
	pthread_mutex_unlock(print_mutex);
}

int ws_send(int conn, char *msg, pthread_mutex_t *print_mutex)
{
	int msg_len = strlen(msg);
	int payld_len;
	const int payld_offset = 2;
	int frame_len;
	char frame[WS_BUFLEN];

	for (int first = 1; msg_len > 0;
	     msg_len -= payld_len, msg += payld_len, first = 0) {
		memset(frame, 0, 128);

		if (msg_len > 125) {
			frame[0] = first;
			payld_len = 125;
		} else {
			frame[0] = 0b10000000 + first;
			payld_len = msg_len;
		}
		frame[1] = payld_len;

		frame_len = payld_offset + payld_len;
		strncpy(&frame[payld_offset], msg, WS_BUFLEN - payld_offset);
		ws_print_frame("ws_send", frame, frame_len, print_mutex);

		if (send_all(conn, frame, frame_len) != 0)
			return -1;
	}

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

int ws_server_response_key(char *server_key, const char *client_key)
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

void *ws_sender(void *arg)
{
	struct ws_thread_data *td = (struct ws_thread_data *)arg;

	int conn = td->conn;
	pthread_mutex_t *restrict print_mutex = td->print_mutex;

	char *restrict data = td->data;
	int *restrict data_ready = td->data_ready;
	pthread_mutex_t *restrict data_mutex = td->data_mutex;
	pthread_cond_t *restrict data_ready_cond = td->data_ready_cond;

	pthread_mutex_lock(data_mutex);
	for (;;) {
		while (!(*data_ready)) {
			pthread_cond_wait(data_ready_cond, data_mutex);
		}
		ws_send(conn, data, print_mutex);
		*data_ready = 0;
	}
	pthread_mutex_unlock(data_mutex);

	return NULL;
}

void *ws_receiver(void *arg)
{
	return NULL;
	if (!WS_DEBUG) {
		return NULL;
	}

	struct ws_thread_data *td = (struct ws_thread_data *)arg;

	int conn = td->conn;
	pthread_mutex_t *restrict print_mutex = td->print_mutex;

	char buf[WS_BUFLEN];
	for (;;) {
		int nrecv = recv(conn, buf, WS_BUFLEN, 0);
		if (nrecv < 0)
			continue;
		ws_print_frame("receiver", buf, nrecv, print_mutex);
	}
	return NULL;
}

void signal_data_ready(int *restrict data_ready, pthread_cond_t *restrict data_ready_cond)
{
	*data_ready = 1;
	pthread_cond_signal(data_ready_cond);
}

void ws_handle_conn(int conn)
{
	if (WS_DEBUG) {
		printf("got connection\n");
	}

	ws_handshake(conn);

	pthread_mutex_t print_mutex;
	pthread_mutex_init(&print_mutex, NULL);

	char data[WS_BUFLEN];
	int data_ready = 0;
	pthread_mutex_t data_mutex;
	pthread_mutex_init(&data_mutex, NULL);
	pthread_cond_t data_ready_cond;
	pthread_cond_init(&data_ready_cond, NULL);

	struct ws_thread_data td;
	td.conn = conn;
	td.print_mutex = &print_mutex;

	td.data = data;
	td.data_ready = &data_ready;
	td.data_mutex = &data_mutex;
	td.data_ready_cond = &data_ready_cond;

	pthread_t recvr_thread;
	pthread_t sendr_thread;
	pthread_create(&recvr_thread, NULL, ws_receiver, (void *)&td);
	pthread_create(&sendr_thread, NULL, ws_sender, (void *)&td);

	for (int i = 0; i < 10; i++) {
		pthread_mutex_lock(&data_mutex);
		snprintf(data, WS_BUFLEN, "hi %d", i);
		signal_data_ready(&data_ready, &data_ready_cond);
		pthread_mutex_unlock(&data_mutex);
		sleep(1);
	}

	pthread_join(recvr_thread, NULL);
	pthread_join(sendr_thread, NULL);
}

int ws_init(int port)
{
	int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	int true = 1;
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &true,
		   sizeof(int));

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if (bind(serv_sock, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	int conn = accept(serv_sock, NULL, NULL);
	ws_handle_conn(conn);

	close(serv_sock);
	return 0;
}
