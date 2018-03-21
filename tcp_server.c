
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#define DEFAULT_PORT 6666
#define DEFAULT_MAX_BACKLOG 5
#define DEFAULT_MUST_ECHO 0

int must_echo = DEFAULT_MUST_ECHO;

static void *worker(void *arg)
{
	int fd = (int )(intptr_t)arg;
	struct sockaddr_storage from;
	const struct sockaddr_in *sin = (const struct sockaddr_in *)&from;
	socklen_t from_len = sizeof from;
	void *ret = NULL;

	if (getpeername(fd, (struct sockaddr *)&from, &from_len) < 0) {
		fprintf(stderr, "Failed to get peername : %s\n", strerror(errno));
		goto err;
	}

	if (sin->sin_family != AF_INET) {
		fprintf(stderr, "Unexpected family : %d\n", sin->sin_family);
		goto close_err;
	}

	fprintf(stderr, "Managing %s:%d\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port));

	for (;;) {
		char input[1024 * 2];
		ssize_t input_size;

		input_size = read(fd, input, sizeof input);
		if (input_size < 0) {
			fprintf(stderr, "Failed to read from %s:%d : %s\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), strerror(errno));
			goto close_err;
		}

		if (input_size == 0)
			break;

		if (must_echo) {
			ssize_t output_size;

			output_size = write(fd, input, input_size);
			if (output_size < 0) {
				fprintf(stderr, "Failed to write to %s:%d : %s\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), strerror(errno));
				goto close_err;
			}

			if (output_size != input_size) {
				fprintf(stderr, "Failed to write to %s:%d %ldb (%ldb done)\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), input_size, output_size);
				goto close_err;
			}
		}
	}

	fprintf(stderr, "Finihed to manage %s:%d\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port));
	ret = arg;
close_err:
	close(fd);
err:
	return ret;
}

int main(int ac, char **av)
{
	int i;
	int sock;
	struct sockaddr_in local;

	memset(&local, 0, sizeof local);
	local.sin_family = AF_INET;

	if (ac >= 2) {

		if (inet_aton(av[1], &local.sin_addr) == 0) {
			fprintf(stderr, "Invalid address : %s\n", av[1]);
			goto err;
		}
	}

	if (ac >= 3) {
		char *ptr;
		long l;

		l = strtol(av[2], &ptr, 0);
		if (errno == EINVAL || errno == ERANGE || l >= UINT16_MAX || l <= 0 || *ptr != 0) {
			fprintf(stderr, "Invalid port : %s\n", av[2]);
			goto err;
		}

		local.sin_port = htons(l);
	} else
		local.sin_port = htons(DEFAULT_PORT);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		fprintf(stderr, "socket : %s\n", strerror(errno));
		goto err;
	}

	i = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof i) < 0) {
		fprintf(stderr, "Failed reuse address : %s\n", strerror(errno));
		goto close_err;
	}

	if (bind(sock, (struct sockaddr *)&local, sizeof local) < 0) {
		fprintf(stderr, "Failed to bind to %s:%d : %s\n", inet_ntoa(local.sin_addr), htons(local.sin_port), strerror(errno));
		goto close_err;
	}

	if (listen(sock, DEFAULT_MAX_BACKLOG) < 0) {
		fprintf(stderr, "Failed to listen on %s:%d : %s\n", inet_ntoa(local.sin_addr), htons(local.sin_port), strerror(errno));
		goto close_err;
	}

	fprintf(stderr, "Accepting from %s:%d\n", inet_ntoa(local.sin_addr), htons(local.sin_port));

	for (;;) {
		int fd;
		struct sockaddr_storage from;
		socklen_t from_len;
		pthread_t tid;

		memset(&from, 0, sizeof from);
		from_len = sizeof from;
		fd = accept(sock, (struct sockaddr *)&from, &from_len);
		if (fd < 0) {
			fprintf(stderr, "Failed to accept on %s:%d : %s\n", inet_ntoa(local.sin_addr), htons(local.sin_port), strerror(errno));
			goto close_err;
		}

		pthread_create(&tid, NULL, worker, (void *)(intptr_t)fd);
		pthread_detach(tid);
	}

	close(sock);
	return 0;

close_err:
	close(sock);
err:
	return 1;
}
