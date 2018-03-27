
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
#define DEFAULT_MUST_ECHO 1
#define DEFAULT_MAX_RECEIVE_SIZE (4 * 1024)

int must_echo = DEFAULT_MUST_ECHO;
size_t max_receive_size = DEFAULT_MAX_RECEIVE_SIZE;

static void *worker(void *arg)
{
	int fd = (int )(intptr_t)arg;
	struct sockaddr_storage from;
	const struct sockaddr_in *sin = (const struct sockaddr_in *)&from;
	socklen_t from_len = sizeof from;
	void *ret = NULL;
	char *receive_buffer;

	if (getpeername(fd, (struct sockaddr *)&from, &from_len) < 0) {
		fprintf(stderr, "Failed to get peername : %s\n", strerror(errno));
		goto err;
	}

	if (sin->sin_family != AF_INET) {
		fprintf(stderr, "Unexpected family : %d\n", sin->sin_family);
		goto close_err;
	}

	receive_buffer = malloc(max_receive_size);
	if (receive_buffer == NULL) {
		fprintf(stderr, "Failed to allocate receive buffer : %s\n", strerror(errno));
		goto close_err;
	}

	fprintf(stderr, "Managing %s:%d\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port));

	for (;;) {
		ssize_t input_size;

		input_size = read(fd, receive_buffer, max_receive_size);
		if (input_size < 0) {
			fprintf(stderr, "Failed to read from %s:%d : %s\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), strerror(errno));
			goto free_err;
		}

		if (input_size == 0)
			break;

		if (must_echo) {
			ssize_t output_size;

			output_size = write(fd, receive_buffer, input_size);
			if (output_size < 0) {
				fprintf(stderr, "Failed to write to %s:%d : %s\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), strerror(errno));
				goto free_err;
			}

			if (output_size != input_size) {
				fprintf(stderr, "Failed to write to %s:%d %ldb (%ldb done)\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port), input_size, output_size);
				goto free_err;
			}
		}
	}

	fprintf(stderr, "Finihed to manage %s:%d\n", inet_ntoa(sin->sin_addr), htons(sin->sin_port));
	ret = arg;

free_err:
	free(receive_buffer);
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
	local.sin_port = htons(DEFAULT_PORT);

	for (i = 1 ; i < ac ; i++) {

		if (strcmp(av[i], "-port") == 0) {
			char *ptr;
			long l;

			if (i + 1 >= ac)
				goto no_arg;

			l = strtol(av[i + 1], &ptr, 0);
			if (errno == EINVAL || errno == ERANGE || l >= UINT16_MAX || l <= 0 || *ptr != 0)
				goto inv_arg;
			local.sin_port = htons(l);
			i++;
			continue;
		}

		if (strcmp(av[i], "-local") == 0) {
			if (i + 1 >= ac)
				goto no_arg;

			if (inet_aton(av[i + 1], &local.sin_addr) == 0)
				goto inv_arg;
			i++;
			continue;
		}

		if (strcmp(av[i], "-size") == 0) {
			char *ptr;

			if (i + 1 >= ac)
				goto no_arg;

			max_receive_size = strtoull(av[i + 1], &ptr, 0);
			if (errno == EINVAL || errno == ERANGE || *ptr != 0)
				goto inv_arg;
			i++;
			continue;
		}

		if (strcmp(av[i], "-noecho") == 0) {
			must_echo = 0;
			continue;
		}

		fprintf(stderr, "Unknown option <%s>\n", av[i]);
		goto usage;
	no_arg:
		fprintf(stderr, "No argument for <%s> option\n", av[i]);
		goto usage;
	inv_arg:
		fprintf(stderr, "Invalid argument for <%s> option : %s\n", av[i], av[i + 1]);
	usage:
		fprintf(stderr, "Usage : %s [-local <bind_address>] [-port <bind_port>] [-size <max_data_size>] [-noecho]\n", av[0]);
		goto err;
	}

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

	fprintf(stderr, "Accepting from %s:%d, max_receive_size = %zd, echo = %d\n", inet_ntoa(local.sin_addr), htons(local.sin_port), max_receive_size, must_echo);

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
