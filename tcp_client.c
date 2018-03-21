
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 6666
#define DEFAULT_MAX_BACKLOG 5

int main(int ac, char **av)
{
	int sock;
	struct sockaddr_in dest;
	const char *host;
	int stop = 0;

	memset(&dest, 0, sizeof dest);
	dest.sin_family = AF_INET;

	if (ac >= 2)
		host = av[1];
	else
		host = DEFAULT_HOST;

	if (inet_aton(host, &dest.sin_addr) == 0) {
		fprintf(stderr, "Invalid address : %s\n", host);
		goto err;
	}

	if (ac >= 3) {
		char *ptr;
		long l;

		l = strtol(av[2], &ptr, 0);
		if (errno == EINVAL || errno == ERANGE || l >= UINT16_MAX || l <= 0 || *ptr != 0) {
			fprintf(stderr, "Invalid port : %s\n", av[2]);
			goto err;
		}

		dest.sin_port = htons(l);
	} else
		dest.sin_port = htons(DEFAULT_PORT);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		fprintf(stderr, "socket : %s\n", strerror(errno));
		goto err;
	}

	fprintf(stderr, "Connecting to %s:%d\n", inet_ntoa(dest.sin_addr), htons(dest.sin_port));

	if (connect(sock, (struct sockaddr *)&dest, sizeof dest) < 0) {
		fprintf(stderr, "Failed to connect to %s:%d : %s\n", inet_ntoa(dest.sin_addr), htons(dest.sin_port), strerror(errno));
		goto close_err;
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

		MAKE_ENTRY(STDIN_FILENO),
		MAKE_ENTRY(sock)
	};

	snprintf(dest_str, sizeof dest_str, "%s:%d", inet_ntoa(dest.sin_addr), htons(dest.sin_port));

	while (stop == 0) {
		size_t i;
		int n;
		char input[1024 * 2];
		ssize_t input_size;
		ssize_t output_size;
		char *input_str;
		char *output_str;
		int input_fd;
		int output_fd;

		n = poll(fds, sizeof fds / sizeof fds[0], -1);
		if (n < 0) {
			fprintf(stderr, "Failed to poll : %s\n", strerror(errno));
			goto close_err;
		}

		for (i = 0 ; i < sizeof fds / sizeof fds[0] ; i++) {

			if (fds[i].revents == 0)
				continue;

			fds[i].revents = 0;

			if (fds[i].fd == STDIN_FILENO) {

				input_str = "<stdin>";
				output_str = dest_str;

				input_fd = STDIN_FILENO;
				output_fd = sock;

			} else if (fds[i].fd == sock) {

				input_str = dest_str;
				output_str = "<stdout>";

				input_fd = sock;
				output_fd = STDOUT_FILENO;
			} else
				continue;

			input_size = read(input_fd, input, sizeof input);
			if (input_size < 0) {
				fprintf(stderr, "Failed to read from %s : %s\n", input_str, strerror(errno));
				goto close_err;
			}
			if (input_size == 0) {
				stop ++;
				break;
			}

			output_size = write(output_fd, input, input_size);
			if (output_size < 0) {
				fprintf(stderr, "Failed to write to %s : %s\n", output_str, strerror(errno));
				goto close_err;
			}

			if (output_size != input_size) {
				fprintf(stderr, "Failed to write to %s %ldb (%ldb done)\n", output_str, input_size, output_size);
				goto close_err;
			}
		}
	}

	close(sock);
	return 0;

close_err:
	close(sock);
err:
	return 1;
}
