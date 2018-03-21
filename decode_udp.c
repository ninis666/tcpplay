
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
	int ret = -1;
	uint32_t s;

	(void)private;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid UDP pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}

	s = htons(hdr->len);
	if (s > len) {
		fprintf(stderr, "Invalid UDP length : %d (%d at least)\n", s, len);
		goto err;
	}
	s -= sizeof hdr[0];

	frame->proto.type = frame_proto_type_udp;
	frame->proto.udp.hdr = *hdr;
	frame->proto.udp.data_size = s;
	frame->proto.udp.data = malloc(s);
	if (frame->proto.udp.data == NULL) {
		fprintf(stderr, "Failed to malloc UDP data : %s\n", strerror(errno));
		goto err;
	}
	memcpy(frame->proto.udp.data, data + sizeof hdr[0], s);

	printf("%*sUDP %d -> %d\n", depth, "", htons(hdr->source), htons(hdr->dest));
	printf("%*sData (%db)\n", depth, "", s);
	rawprint(stdout, depth + 1, data + sizeof hdr[0], s, 8, 4);

	ret = 0;
err:
	return ret;
}
