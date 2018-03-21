

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <string.h>

#include "decode_eth.h"
#include "decode_ip.h"
#include "decode_arp.h"
#include "frame.h"

int decode_eth(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	const struct ether_header *hdr = data;
	int ret = -1;
	(void)private;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid ETHERNET pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}

	if (sizeof hdr->ether_shost != sizeof frame->hw.source)
		abort();

	if (sizeof hdr->ether_dhost != sizeof frame->hw.dest)
		abort();

	memcpy(frame->hw.dest, hdr->ether_dhost, sizeof hdr->ether_dhost);
	memcpy(frame->hw.source, hdr->ether_shost, sizeof hdr->ether_shost);

	frame_print_hw(stdout, depth, &frame->hw);

	switch (htons(hdr->ether_type)) {
	default:
		fprintf(stderr, "!!! Unexpected ETHERNET type : %#06x\n", htons(hdr->ether_type));
		goto err;

	case ETHERTYPE_ARP:
		ret = decode_arp(frame, depth + 1, data + sizeof hdr[0], len - sizeof hdr[0], private);
		break;

	case ETHERTYPE_IP:
		ret = decode_ip(frame, depth + 1, data + sizeof hdr[0], len - sizeof hdr[0], private);
		break;
	}

err:
	return ret;
}
