
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "decode_tcp.h"
#include "rawprint.h"

int decode_tcp(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	const struct tcphdr *hdr = data;
	uint16_t opt_size;
	uint8_t *opt;
	uint32_t app_data_size;
	uint8_t *app_data;
	(void)private;
	(void)depth;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid TCP pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}


	if (hdr->doff < (sizeof hdr[0] / 4)) {
		fprintf(stderr, "Invalid TCP header size : %d (at least %zd expected)\n", hdr->doff, sizeof hdr[0] / 4);
		goto err;
	}


	opt_size = 4 * hdr->doff - sizeof hdr[0];

	if (len < (sizeof hdr[0] + opt_size)) {
		fprintf(stderr, "Unexpected TCP pload size : %d (at least %zd expected)\n", len, sizeof hdr[0] + opt_size);
		goto err;
	}
	app_data_size = len - (sizeof hdr[0] + opt_size);

	if (opt_size == 0)
		opt = NULL;
	else {
		opt = malloc(opt_size);
		if (opt == NULL) {
			fprintf(stderr, "Failed to allocate TCP options : %s\n", strerror(errno));
			goto err;
		}
		memcpy(opt, data + sizeof hdr[0], opt_size);
	}

	if (app_data_size == 0)
		app_data = NULL;
	else {
		app_data = malloc(app_data_size);
		if (app_data == NULL) {
			fprintf(stderr, "Failed to allocate TCP data : %s\n", strerror(errno));
			goto free_opt_err;
		}

		memcpy(app_data, data + sizeof hdr[0] + opt_size, app_data_size);
	}

	frame->proto.type = frame_proto_type_tcp;
	frame->proto.tcp.hdr = *hdr;
	frame->proto.tcp.opt_size = opt_size;
	frame->proto.tcp.opt = opt;
	frame->app.size = app_data_size;
	frame->app.data = app_data;

	return 0;

free_opt_err:
	free(opt);
err:
	return -1;
}
