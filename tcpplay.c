
#include <stdio.h>
#include <stdlib.h>
#include <pcap/pcap.h>

#include "decode.h"
#include "rawprint.h"

int main(int ac, char **av)
{
	pcap_t *pc;
	char errbuff[PCAP_ERRBUF_SIZE];
	char *from;
	decode_fun_t decode;

	if (ac >= 2) {
		from = av[1];
		pc = pcap_open_offline(from, errbuff);
	} else {
		from = "stdin";
		pc = pcap_fopen_offline(stdin, errbuff);
	}

	if (pc == NULL) {
		fprintf(stderr, "Failed to open <%s> input : %s\n", from, errbuff);
		goto err;
	}

	decode = decode_get(from, pcap_datalink(pc));
	if (decode == NULL)
		goto close_err;

	for (;;) {
		struct pcap_pkthdr *hdr;
		const u_char *data;
		struct frame frame;
		int res;

		res = pcap_next_ex(pc, &hdr, &data);
		if (res == 0)
			continue;
		if (res == -2)
			break;

		if (res == -1) {
			fprintf(stderr, "Failed to read from <%s> : %s", from, pcap_geterr(pc));
			goto close_err;
		}

		if (res != 1)
			abort();

		printf("++++\n");

		if (hdr->caplen < hdr->len) {
			fprintf(stderr, "Packet was not fully captured\n");
			continue;
		}

		decode(&frame, 0, data, hdr->len, NULL);
	}

	return 0;

close_err:
	pcap_close(pc);
err:
	return 1;
}
