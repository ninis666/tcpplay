
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include "decode_arp.h"

int decode_arp(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	const struct ether_arp *hdr = data;
	int ret = -1;
	(void)private;
	(void)depth;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid ARP pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}

	if (htons(hdr->arp_hrd) != ARPHRD_ETHER) {
		fprintf(stderr, "Unexpected ARP hw address format : %d (%d expected)\n", htons(hdr->arp_hrd), ARPHRD_ETHER);
		goto err;
	}

	if (htons(hdr->arp_pro) != ETHERTYPE_IP) {
		fprintf(stderr, "Unexpected ARP protocol address format : %#06x (%#06x expected)\n", htons(hdr->arp_pro), ETHERTYPE_IP);
		goto err;
	}

	if (hdr->arp_hln != sizeof hdr->arp_sha || hdr->arp_hln != sizeof hdr->arp_tha) {
		fprintf(stderr, "Unexpected ARP hw address length : %d (%zd expected)\n", hdr->arp_hln, sizeof hdr->arp_sha);
		goto err;
	}

	if (hdr->arp_pln != sizeof hdr->arp_spa || hdr->arp_pln != sizeof hdr->arp_tpa) {
		fprintf(stderr, "Unexpected ARP protocol address length : %d (%zd expected)\n", hdr->arp_hln, sizeof hdr->arp_spa);
		goto err;
	}


	if (hdr->arp_hln != sizeof frame->net.arp.hw_source) {
		fprintf(stderr, "Unexpected ARP protocol address length : %d (%zd expected)\n", hdr->arp_hln, sizeof hdr->arp_spa);
		goto err;
	}

	if (sizeof hdr->arp_sha != sizeof frame->net.arp.hw_source)
		abort();

	if (sizeof hdr->arp_tha != sizeof frame->net.arp.hw_dest)
		abort();

	if (sizeof hdr->arp_spa != sizeof frame->net.arp.ip_source)
		abort();

	if (sizeof hdr->arp_tpa != sizeof frame->net.arp.ip_dest)
		abort();

	frame->net.type = frame_net_type_arp;
	frame->net.arp.opcode = htons(hdr->arp_op);
	memcpy(frame->net.arp.hw_source, hdr->arp_sha, sizeof frame->net.arp.hw_source);
	memcpy(frame->net.arp.hw_dest, hdr->arp_tha, sizeof frame->net.arp.hw_dest);
	memcpy(&frame->net.arp.ip_source, hdr->arp_spa, sizeof frame->net.arp.ip_source);
	memcpy(&frame->net.arp.ip_dest, hdr->arp_tpa, sizeof frame->net.arp.ip_dest);
	ret = 0;
err:
	return ret;
}

