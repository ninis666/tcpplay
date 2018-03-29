
#include <stdio.h>
#include <stdlib.h>
#include <pcap/pcap.h>
#include <string.h>

#include "decode.h"
#include "rawprint.h"
#include "frame_list.h"
#include "session.h"

int cmd_list_session(struct session_table *session_table, int ac, char **av)
{
	(void)ac;
	(void)av;
	printf("Session found :\n");
	session_table_dump(stdout, 0, session_table, 0);
	return 0;
}

int cmd_dump_session(struct session_table *session_table, int ac, char **av)
{
	(void)ac;
	(void)av;
	printf("Session found :\n");
	session_table_dump(stdout, 0, session_table, 1);
	return 0;
}

static const struct {
	const char *name;
	int(*fun)(struct session_table *session_table, int ac, char **av);
} cmd_table[] = {
	{ "list", cmd_list_session },
	{ "dump", cmd_dump_session },
};

int main(int ac, char **av)
{
	pcap_t *pc;
	char errbuff[PCAP_ERRBUF_SIZE];
	char *from;
	decode_fun_t decode;
	struct frame_table frame_table;
	struct session_table session_table;
	int(*cmd_fun)(struct session_table *session_table, int ac, char **av) = NULL;
	int ret = 1;

	for (int i = 1 ; i < ac ; i++) {
		if (strcmp(av[i], "help") == 0 || strcmp(av[i], "-help") == 0)
			goto usage;
	}

	if (ac >= 2 && strcmp(av[1], "-") != 0) {
		from = av[1];
		pc = pcap_open_offline(from, errbuff);
	} else {
		from = "stdin";
		pc = pcap_fopen_offline(stdin, errbuff);
	}

	if (ac >= 3) {
		cmd_fun = NULL;
		for (size_t i = 0 ; i < sizeof cmd_table / sizeof cmd_table[0] ; i ++) {
			if (strcmp(av[2], cmd_table[i].name) == 0) {
				cmd_fun = cmd_table[i].fun;
				break;
			}
		}

		if (cmd_fun == NULL) {
			fprintf(stderr, "Unknown command : %s\n", av[2]);
			goto usage;
		}

	} else
		cmd_fun = cmd_list_session;

	if (pc == NULL) {
		fprintf(stderr, "Failed to open <%s> input : %s\n", from, errbuff);
		goto err;
	}

	decode = decode_get(from, pcap_datalink(pc));
	if (decode == NULL)
		goto close_err;

	if (frame_table_init(&frame_table) < 0)
		goto close_err;

	if (session_table_init(&session_table) < 0)
		goto free_frame_table_err;

	for (;;) {
		struct pcap_pkthdr *hdr;
		const u_char *data;
		struct frame_node *frame_node;
		int res;

		res = pcap_next_ex(pc, &hdr, &data);
		if (res == 0)
			continue;
		if (res == -2)
			break;

		if (res == -1) {
			fprintf(stderr, "Failed to read from <%s> : %s", from, pcap_geterr(pc));
			goto free_session_table_err;
		}

		if (res != 1)
			abort();

		if (hdr->caplen < hdr->len) {
			fprintf(stderr, "Packet was not fully captured\n");
			continue;
		}

		frame_node = frame_node_new(&frame_table, &hdr->ts);
		if (frame_node == NULL)
			goto free_session_table_err;

		if (decode(&frame_node->frame, 0, data, hdr->len, NULL) >= 0) {
			int res;

			res = session_process_frame(&session_table, &frame_table.used_list, frame_node);
			if (res < 0)
				goto free_session_table_err;
			if (res == 0)
				frame_node_recycle(&frame_table, frame_node);
		}
	}

	ret = cmd_fun(&session_table, ac, av);

free_session_table_err:
	session_table_free(&session_table);
free_frame_table_err:
	frame_table_free(&frame_table);
close_err:
	pcap_close(pc);
err:
	return ret;

usage:
	fprintf(stderr, "Usage: %s <file.pcap | - > [ cmd ]\n", av[0]);
	fprintf(stderr, "cmd:\n");
	for (size_t i = 0 ; i < sizeof cmd_table / sizeof cmd_table[0] ; i ++)
		fprintf(stderr, "%*s%s\n", 4, "", cmd_table[i].name);
	return 1;
}
