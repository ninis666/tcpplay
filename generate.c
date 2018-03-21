
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "generate_string_tables.h"

#define DEV_RAND "/dev/urandom"

int io_block(int fd, void *buffer, const size_t count, ssize_t (*fun)(int fd, void *data, size_t data_size))
{
	size_t done = 0;
	ssize_t n;
	int ret = -1;

	do {
		n = fun(fd, buffer + done, count - done);
		if (n < 0)
			goto err;
		done += (size_t )n;

	} while (count != done);

	ret = 0;
err:
	return ret;
}

int read_block(int fd, void *buffer, const size_t count)
{
	return io_block(fd, buffer, count, read);
}

static ssize_t unconst_write(int fd, void *data, size_t data_size)
{
	return write(fd, data, data_size);
}

int write_block(int fd, void *buffer, const size_t count)
{
	return io_block(fd, buffer, count, unconst_write);
}

void convert_block(int fd, const void *buffer, const size_t count)
{
	static char *hex_table[] = {
		generate_hexa_string
	};
	size_t offset;

	for (offset = 0 ; offset < count ; offset ++) {
		const uint8_t *ptr = buffer + offset;
		write_block(fd, hex_table[*ptr], 2);
	}

	write_block(fd, "\n", 1);

}

int main(int ac, char **av)
{
	uint16_t n;
	uint16_t nloop;
	uint16_t loop;
	int fd_in;
	int ret = 1;
	uint8_t *input;

	if (ac >= 2) {
		unsigned long long l;
		char *ptr;

		l = strtoull(av[1], &ptr, 0);
		if (l >= UINT16_MAX || *ptr != 0 || errno == EINVAL || errno == ERANGE) {
			fprintf(stderr, "Invalid lenght : %s", av[1]);
			goto err;
		}

		n = (uint16_t )(l / 2);
	} else
		n = (1024 * 2) / 2;

	if (ac >= 3) {
		unsigned long long l;
		char *ptr;

		l = strtoull(av[1], &ptr, 0);
		if (l >= UINT16_MAX || *ptr != 0 || errno == EINVAL || errno == ERANGE) {
			fprintf(stderr, "Invalid lenght : %s", av[1]);
			goto err;
		}

		nloop = (uint16_t )l;
	} else
		nloop = 1;

	fd_in = open(DEV_RAND, O_RDONLY);
	if (fd_in < 0) {
		fprintf(stderr, "Failed to open %s : %s\n", DEV_RAND, strerror(errno));
		goto err;
	}

	input = malloc(n);
	if (input == NULL) {
		fprintf(stderr, "Failed to malloc %db : %s\n", n, strerror(errno));
		goto close_err;
	}

	loop = 0;
	for (;;) {

		if (nloop > 0) {
			if (loop >= nloop)
				break;
			loop ++;
		}

		if (read_block(fd_in, input, n) < 0) {
			fprintf(stderr, "Failed to read from %s : %s\n", DEV_RAND, strerror(errno));
			goto free_err;
		}

		convert_block(STDOUT_FILENO, input, n);
	}

	ret = 0;

free_err:
	free(input);
close_err:
	close(fd_in);
err:
	return ret;
}
