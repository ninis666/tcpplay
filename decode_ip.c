
#include <stdio.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "decode_ip.h"
#include "decode_tcp.h"
#include "decode_udp.h"

int decode_ip(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	const struct iphdr *iphdr = data;
	int ret = -1;

	if (len < sizeof iphdr[0]) {
		fprintf(stderr, "Invalid IP pload size : %d (%zd at least)\n", len, sizeof iphdr[0]);
		goto err;
	}

	if (iphdr->ihl < sizeof iphdr[0] / 4) {
		fprintf(stderr, "Invalid IP ihl : %d (%zd at least)\n", len, sizeof iphdr[0] / 4);
		goto err;
	}

	if (htons(iphdr->tot_len) > len) {
		fprintf(stderr, "Invalid IP tot_len : %d (%d at least)\n", htons(iphdr->tot_len), len);
		goto err;
	}

	if (sizeof frame->net.ip.source != sizeof iphdr->saddr)
		abort();

	if (sizeof frame->net.ip.dest != sizeof iphdr->daddr)
		abort();

	frame->net.type = frame_net_type_ip;
	frame->net.ip.source = (struct in_addr){.s_addr = iphdr->saddr};
	frame->net.ip.dest = (struct in_addr){.s_addr = iphdr->daddr};

	frame_print_net(stdout, depth, &frame->net);

	switch (iphdr->protocol) {
	default:
		fprintf(stderr, "Unexpected IP protocol : %d\n", iphdr->protocol);
		goto err;

	case IPPROTO_TCP:
		ret = decode_tcp(frame, depth + 1, data + iphdr->ihl * 4, len - iphdr->ihl * 4, private);
		break;

	case IPPROTO_UDP:
		ret = decode_udp(frame, depth + 1, data + iphdr->ihl * 4, len - iphdr->ihl * 4, private);
		break;
	}

	frame_print_proto(stdout, depth + 1, &frame->proto);

err:
	return ret;
}
