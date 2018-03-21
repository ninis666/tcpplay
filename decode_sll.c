
#include <pcap/pcap.h>
#include <pcap/sll.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include "decode_sll.h"
#include <string.h>

#include "decode_ip.h"
#include "decode_arp.h"

int decode_sll(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private)
{
	int ret = -1;
	const struct sll_header *hdr = data;

	if (len < sizeof hdr[0]) {
		fprintf(stderr, "Invalid SLL pload size : %d (%zd at least)\n", len, sizeof hdr[0]);
		goto err;
	}

	switch (htons(hdr->sll_hatype))  {
	default:
		fprintf(stderr, "Unexpected sll_pkttype : %#06x\n", hdr->sll_hatype);
		goto err;

	case ARPHRD_ETHER:
	{
		const uint32_t halen = htons(hdr->sll_halen);

		if (halen > sizeof hdr->sll_addr) {
			fprintf(stderr, "Unexpected sll_halen : %d (sll max = %zd)\n", halen, sizeof hdr->sll_addr);
			goto err;
		}

		if (halen > sizeof frame->hw.dest) {
			fprintf(stderr, "Unexpected sll_halen : %d (hw dest max = %zd)\n", halen, sizeof frame->hw.dest);
			goto err;
		}

		if (halen > sizeof frame->hw.source) {
			fprintf(stderr, "Unexpected sll_halen : %d (hw source max = %zd)\n", halen, sizeof frame->hw.source);
			goto err;
		}

		switch (htons(hdr->sll_pkttype)) {
		default:
			fprintf(stderr, "Unexpected sll_pkttype : %#06x\n", hdr->sll_pkttype);
			goto err;

		case LINUX_SLL_HOST: /* RX */
			memcpy(frame->hw.source, hdr->sll_addr, halen);
			memset(frame->hw.dest, 0x00, sizeof frame->hw.dest);
			break;

		case LINUX_SLL_OUTGOING: /* TX */
			memcpy(frame->hw.dest, hdr->sll_addr, halen);
			memset(frame->hw.source, 0x00, sizeof frame->hw.source);
			break;

		case LINUX_SLL_BROADCAST:
			memcpy(frame->hw.source, hdr->sll_addr, halen);
			memset(frame->hw.dest, 0xFF, sizeof frame->hw.dest);
			break;
		}
		break;
	}

	case ARPHRD_LOOPBACK:
		memset(frame->hw.dest, 0x00, sizeof frame->hw.dest);
		memset(frame->hw.source, 0x00, sizeof frame->hw.source);
		break;
	}

	switch (htons(hdr->sll_protocol)) {
	default:
		fprintf(stderr, "!!! Unexpected sll_protocol : %#06x\n", htons(hdr->sll_protocol));
		goto err;

	case ETHERTYPE_IP:
		ret = decode_ip(frame, depth + 1, data + sizeof hdr[0], len - sizeof hdr[0], private);
		break;

	case ETHERTYPE_ARP:
		ret = decode_arp(frame, depth + 1, data + sizeof hdr[0], len - sizeof hdr[0], private);
		break;
	}

err:
	return ret;
}
