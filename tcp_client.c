
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 6666
#define DEFAULT_MAX_SEND_SIZE (4 * 1024)

int main(int ac, char **av)
{
	int sock;
	struct sockaddr_in dest;
	const char *host;
	const char *input;
	int stop = 0;
	char *send_buffer;
	size_t max_send_size = 1024 * 4;
	int input_fd;
	int ret = 1;

	memset(&dest, 0, sizeof dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(DEFAULT_PORT);
	host = DEFAULT_HOST;
	max_send_size = DEFAULT_MAX_SEND_SIZE;
	input = NULL;
	input_fd = -1;

	for (int i = 1 ; i < ac ; i++) {

		if (strcmp(av[i], "-port") == 0) {
			char *ptr;
			long l;

			if (i + 1 >= ac)
				goto no_arg;

			l = strtol(av[i + 1], &ptr, 0);
			if (errno == EINVAL || errno == ERANGE || l >= UINT16_MAX || l <= 0 || *ptr != 0)
				goto inv_arg;

			dest.sin_port = htons(l);
			i++;
			continue;
		}

		if (strcmp(av[i], "-host") == 0) {
			if (i + 1 >= ac)
				goto no_arg;

			host = av[i + 1];
			i++;
			continue;
		}

		if (strcmp(av[i], "-size") == 0) {
			char *ptr;

			if (i + 1 >= ac)
				goto no_arg;

			max_send_size = strtoull(av[i + 1], &ptr, 0);
			if (errno == EINVAL || errno == ERANGE || *ptr != 0)
				goto inv_arg;
			i++;
			continue;
		}

		if (strcmp(av[i], "-input") == 0) {
			input = av[i + 1];
			i++;
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
		fprintf(stderr, "Usage : %s [-host <target_address>] [-port <target_port>] [-size <max_data_size>]\n", av[0]);
		goto err;
	}

	if (inet_aton(host, &dest.sin_addr) == 0) {
		fprintf(stderr, "Invalid address : %s\n", host);
		goto err;
	}

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		fprintf(stderr, "socket : %s\n", strerror(errno));
		goto err;
	}

	send_buffer = malloc(max_send_size);
	if (send_buffer == NULL) {
		fprintf(stderr, "Failed to allocate send buffer : %s\n", strerror(errno));
		goto err;
	}

	input_fd = STDIN_FILENO;
	if (input != NULL) {
		input_fd = open(input, O_RDONLY);
		if (input_fd < 0) {
			fprintf(stderr, "Failed to open input file <%s> : %s\n", input, strerror(errno));
			goto free_err;
		}
	} else
		input = "<stdin>";

	fprintf(stderr, "Connecting to %s:%d, max_send_size = %zd, input = %s\n", inet_ntoa(dest.sin_addr), htons(dest.sin_port), max_send_size, input);

	if (connect(sock, (struct sockaddr *)&dest, sizeof dest) < 0) {
		fprintf(stderr, "Failed to connect to %s:%d : %s\n", inet_ntoa(dest.sin_addr), htons(dest.sin_port), strerror(errno));
		goto close_input_err;
	}

	fprintf(stderr, "Connected to %s:%d\n", inet_ntoa(dest.sin_addr), htons(dest.sin_port));

	char dest_str[sizeof "xxx.xxx.xxx.xxx:xxxxx"];
	struct pollfd fds[] = {

#define MAKE_ENTRY(f)						\
		{						\
			.fd = f,				\
			.events = POLLIN | POLLHUP | POLLERR,	\
			.revents = 0				\
		}

		MAKE_ENTRY(input_fd),
		MAKE_ENTRY(sock)
	};

	snprintf(dest_str, sizeof dest_str, "%s:%d", inet_ntoa(dest.sin_addr), htons(dest.sin_port));

	while (stop == 0) {
		size_t i;
		int n;
		ssize_t from_size;
		ssize_t to_size;
		const char *from_str;
		const char *to_str;
		int from_fd;
		int to_fd;

		n = poll(fds, sizeof fds / sizeof fds[0], -1);
		if (n < 0) {
			fprintf(stderr, "Failed to poll : %s\n", strerror(errno));
			goto close_err;
		}

		for (i = 0 ; i < sizeof fds / sizeof fds[0] ; i++) {

			if (fds[i].revents == 0)
				continue;

			fds[i].revents = 0;

			if (fds[i].fd == input_fd) {

				from_str = input;
				from_fd = input_fd;

				to_str = dest_str;
				to_fd = sock;

			} else if (fds[i].fd == sock) {

				from_str = dest_str;
				from_fd = sock;

				to_str = "<stdout>";
				to_fd = STDOUT_FILENO;

			} else
				continue;

			from_size = read(from_fd, send_buffer, max_send_size);
			if (from_size < 0) {
				fprintf(stderr, "Failed to read from %s : %s\n", from_str, strerror(errno));
				goto close_err;
			}
			if (from_size == 0) {
				stop ++;
				break;
			}

			printf("%s -> %s (%zdb)\n", from_str, to_str, from_size);

			to_size = write(to_fd, send_buffer, from_size);
			if (to_size < 0) {
				fprintf(stderr, "Failed to write to %s : %s\n", to_str, strerror(errno));
				goto close_err;
			}

			if (to_size != from_size) {
				fprintf(stderr, "Failed to write to %s %ldb (%ldb done)\n", to_str, from_size, to_size);
				goto close_err;
			}
		}
	}

	ret = 0;

close_err:
	close(sock);

close_input_err:
	if (input_fd != STDIN_FILENO)
		close(input_fd);
free_err:
	free(send_buffer);
err:
	return ret;
}
