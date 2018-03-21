
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "decode_udp.h"
#include "rawprint.h"

int decode_udp(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	const struct udphdr *hdr = data;
	uint32_t app_data_size;
	uint8_t *app_data;
	(void)depth;
	(void)private;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid UDP pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}

	app_data_size = htons(hdr->len);
	if (app_data_size > len) {
		fprintf(stderr, "Invalid UDP length : %d (%d at least)\n", app_data_size, len);
		goto err;
	}

	if (app_data_size < sizeof hdr[0]) {
		fprintf(stderr, "Unexpected UDP pload size : %d (at least %zd expected)\n", app_data_size, sizeof hdr[0]);
		goto err;
	}

	app_data_size -= sizeof hdr[0];

	if (app_data_size == 0)
		app_data = NULL;
	else {
		app_data = malloc(app_data_size);
		if (app_data == NULL) {
			fprintf(stderr, "Failed to allocate UDP data : %s\n", strerror(errno));
			goto err;
		}
		memcpy(app_data, data + sizeof hdr[0], app_data_size);
	}

	frame->proto.type = frame_proto_type_udp;
	frame->proto.udp.hdr = *hdr;
	frame->app.size = app_data_size;
	frame->app.data = app_data;
	return 0;

err:
	return -1;
}
