
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#include "replayer.h"
#include "rawprint.h"

void replayer_deinit(struct replayer *replayer)
{
	if (replayer->sock >= 0)
		close(replayer->sock);
	if (replayer->dist_sock >= 0 && replayer->dist_sock != replayer->sock)
		close(replayer->dist_sock);
	replayer->sock = -1;
}

int replayer_init(struct replayer *replayer, const int server_mode, const struct in_addr local_addr, const uint16_t local_port,	const struct in_addr distant_addr, const uint16_t distant_port, const struct session_tx_list *tx_list)
{
	int i;

	memset(replayer, 0, sizeof replayer[0]);
	replayer->sock = -1;

	replayer->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (replayer->sock < 0) {
		fprintf(stderr, "Failed to create socket : %s\n", strerror(errno));
		goto err;
	}

	replayer->local.sin_family = AF_INET;
	replayer->local.sin_addr = local_addr;
	replayer->local.sin_port = htons(local_port);

	replayer->distant.sin_family = AF_INET;
	replayer->distant.sin_addr = distant_addr;
	replayer->distant.sin_port = htons(distant_port);

	i = 1;
	if (setsockopt(replayer->sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof i) < 0) {
		fprintf(stderr, "Failed reuse address : %s\n", strerror(errno));
		goto close_err;
	}

	if (bind(replayer->sock, (struct sockaddr *)&replayer->local, sizeof replayer->local) < 0) {
		fprintf(stderr, "Failed to bind to %s:%d : %s\n", inet_ntoa(local_addr), local_port, strerror(errno));
		goto close_err;
	}

	if (server_mode) {
		if (listen(replayer->sock, 1) < 0) {
			fprintf(stderr, "Failed to listen : %s\n", strerror(errno));
			goto close_err;
		}

		replayer->flags |= REPLAYER_FLAGS_SERVER;
	}

	i = fcntl(replayer->sock, F_GETFL);
	if (fcntl(replayer->sock, F_SETFL, i | O_NONBLOCK) < 0) {
		fprintf(stderr, "Failed to set NONBLOCK flag : %s\n", strerror(errno));
		goto close_err;
	}

	replayer->tx_list = tx_list;
	return 0;

close_err:
	close(replayer->sock);
	replayer->sock = -1;
err:
	return -1;
}

static int replay_start(struct replayer *replayer)
{

	if ((replayer->flags & REPLAYER_FLAGS_SERVER) != 0) {
		struct sockaddr_in sin;
		socklen_t len = sizeof sin;
		int fd;

		fd = accept(replayer->sock, (struct sockaddr *)&sin, &len);
		if (fd < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto pending;
			fprintf(stderr, "Failed to accept : %s\n", strerror(errno));
			goto err;
		}

		if (replayer->distant.sin_addr.s_addr != INADDR_ANY || replayer->distant.sin_port != 0) {
			int error = 0;
			if (len != sizeof replayer->distant)
				error ++;
			else if (sin.sin_family != replayer->distant.sin_family)
				error ++;
			else if (sin.sin_addr.s_addr != replayer->distant.sin_addr.s_addr)
				error ++;
			else if (sin.sin_port != replayer->distant.sin_port)
				error ++;
			if (error != 0) {
				close(fd);
				goto err;
			}

			goto pending;
		}

		replayer->distant = sin;
		replayer->dist_sock = fd;

	} else {
		if (connect(replayer->sock, (struct sockaddr *)&replayer->distant, sizeof replayer->distant) < 0) {
			if (errno == EINPROGRESS || errno == EALREADY)
				goto pending;
			fprintf(stderr, "Failed to connect to %s:%d : %s\n", inet_ntoa(replayer->distant.sin_addr), htons(replayer->distant.sin_port), strerror(errno));
			goto err;
		}

		replayer->dist_sock = replayer->sock;
	}

	return 1;
pending:
	return 0;
err:
	return -1;
}

static int do_send(int fd, const uint8_t *data, const size_t size)
{
	size_t done = 0;

	while (done < size) {
		ssize_t t;

		t = write(fd, data + done, size - done);
		if (t < 0)
			goto err;
		done += (size_t)t;
	}

	return 0;

err:
	return -1;
}

static int replay_send(struct replayer *replayer, const struct timeval *now)
{
	struct timeval next_dt;
	struct timeval real_dt;
	uint8_t *data;
	size_t size;

	if (replayer->last_tx_ts.tv_sec == 0 && replayer->last_tx_ts.tv_usec == 0) {
		replayer->next_tx = replayer->tx_list->first;
		replayer->last_tx_ts = (now != NULL) ? *now : (struct timeval){ -1, -1 };
	}

	if (replayer->next_tx == NULL)
		goto done;

	if (replayer->tx_list->first == NULL)
		abort();

	if (now != NULL) {
		timersub(&replayer->next_tx->tx.ts, &replayer->tx_list->first->tx.ts, &next_dt);
		timersub(now, &replayer->last_tx_ts, &real_dt);
		if (timercmp(&real_dt, &next_dt, <))
			goto idle;
	} else
		next_dt = (struct timeval){ 0, 0 };

	data = replayer->next_tx->tx.buffer->data.stream;
	size = replayer->next_tx->tx.buffer->to - replayer->next_tx->tx.buffer->from + 1;

	printf("[%ld, %ld] Tx %s:%d\n", real_dt.tv_sec, real_dt.tv_usec, inet_ntoa(replayer->distant.sin_addr), htons(replayer->distant.sin_port));
	rawprint(stdout, 1, data, size, 8, 4);

	if (do_send(replayer->dist_sock, data, size) < 0) {
		fprintf(stderr, "Failed to send : %s\n", strerror(errno));
		goto err;
	}

	replayer->last_tx_ts = (now != NULL) ? *now : (struct timeval){ -1, -1 };
	replayer->next_tx = replayer->next_tx->next;
	return 1;

idle:
	return 0;
done:
	return -1;
err:
	return -1;
}

static int replay_receive(struct replayer *replayer, const struct timeval *now_ptr)
{
	ssize_t size;
	struct timeval now;
	struct timeval real_dt;

	if (now_ptr == NULL) {
		gettimeofday(&now, NULL);
		now_ptr = &now;
	}

	if (replayer->first_rx_ts.tv_sec == 0 && replayer->first_rx_ts.tv_usec == 0)
		replayer->first_rx_ts = *now_ptr;
	timersub(now_ptr, &replayer->first_rx_ts, &real_dt);

	for (;;) {
		char data[1024];

		size = read(replayer->dist_sock, data, sizeof data);
		if (size == 0) {
			fprintf(stderr, "Distant %s:%d seems to be closed\n", inet_ntoa(replayer->distant.sin_addr), htons(replayer->distant.sin_port));
			goto err;
		}

		if (size < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				goto idle;
			fprintf(stderr, "Failed to read from %s:%d : (%d) %s\n", inet_ntoa(replayer->distant.sin_addr), htons(replayer->distant.sin_port), errno, strerror(errno));
			goto err;
		}

		/*
		 * TODO : Check received data with what we're expecting ?
		 */
		printf("[%ld, %ld] Rx %s:%d\n", real_dt.tv_sec, real_dt.tv_usec, inet_ntoa(replayer->distant.sin_addr), htons(replayer->distant.sin_port));
		rawprint(stdout, 1, data, size, 8, 4);
	}

	return 1;

idle:
	return 0;
err:
	return -1;
}

int replayer_connected(struct replayer *replayer)
{
	if ((replayer->flags & REPLAYER_FLAGS_CONNECTED) == 0)
		return 0;
	return 1;
}

int replayer_loop(struct replayer *replayer, const struct timeval *now)
{
	int res;

	if ((replayer->flags & REPLAYER_FLAGS_CONNECTED) == 0) {
		res = replay_start(replayer);
		if (res < 0)
			goto err;
		if (res == 0)
			goto idle;
		replayer->flags |= REPLAYER_FLAGS_CONNECTED;

		if (now == NULL) /* On interactive mode, dont send now ! */
			goto idle;
	}

	res = replay_send(replayer, now);
	if (res < 0)
		goto err;
	if (res == 0)
		goto idle;

	res = replay_receive(replayer, now);
	if (res < 0)
		goto err;
	if (res == 0)
		goto idle;


	return 0;

idle:
	return 1;
err:
	return -1;
}
