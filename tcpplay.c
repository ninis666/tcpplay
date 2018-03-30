
#include <stdio.h>
#include <stdlib.h>
#include <pcap/pcap.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "decode.h"
#include "rawprint.h"
#include "frame_list.h"
#include "session.h"
#include "replayer.h"

static int cmd_list_session(struct session_table *session_table, int ac, char **av)
{
	const char *type;

	type = NULL;
	for (int i = 1 ; i < ac ; i ++) {
		if (strcmp(av[i], "-type") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			type = av[i + 1];
			i++;
		} else {
			fprintf(stderr, "Unknown option for <%s> command : <%s>\n", av[0], av[i]);
			goto usage;
		}
		continue;

	no_arg:
		fprintf(stderr, "No argument for <%s> option\n", av[0]);
	usage:
		fprintf(stderr, "Usage : %s [-type]\n", av[0]);
		return 1;
	}

	printf("Session found :\n");
	session_table_dump(stdout, 0, session_table, type, (struct in_addr){INADDR_ANY}, 0, 0);
	return 0;
}

static int str2addr_port(const char *str, struct in_addr *addr, uint16_t *port)
{
	char tmp[sizeof "255.255.255.255:65535"];
	char *ptr;
	char *end;
	unsigned long ul;

	if (snprintf(tmp, sizeof tmp, "%s", str) >= (int )sizeof tmp)
		goto err;

	ptr = strchr(tmp, ':');
	*ptr = 0;

	if (inet_aton(tmp, addr) != 1)
		goto err;

	ul = strtoul(ptr + 1, &end, 10);
	if (errno == EINVAL || errno == ERANGE)
		goto err;

	if (*ptr != 0 || ul >= UINT16_MAX)
		goto err;

	*port = (uint16_t)ul;
	return 0;

err:
	return -1;
}

static int cmd_dump_session(struct session_table *session_table, int ac, char **av)
{
	const char *type;
	const char *host;
	struct in_addr addr;
	uint16_t port;

	type = NULL;
	host = NULL;
	for (int i = 1 ; i < ac ; i ++) {
		if (strcmp(av[i], "-type") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			type = av[i + 1];
			i++;
		} else if (strcmp(av[i], "-host") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			host = av[i + 1];
			i++;
		} else {
			fprintf(stderr, "Unknown option for <%s> command : <%s>\n", av[0], av[i]);
			goto usage;
		}
		continue;

	no_arg:
		fprintf(stderr, "No argument for <%s> option\n", av[0]);
	usage:
		fprintf(stderr, "Usage : %s [-type] [-host <addr:port>]\n", av[0]);
		return 1;
	}

	if (host == NULL) {
		addr.s_addr = INADDR_ANY;
		port = 0;
	} else if (str2addr_port(host, &addr, &port) < 0) {
		fprintf(stderr, "Invalid host : <%s>\n", host);
		goto usage;
	}

	session_table_dump(stdout, 0, session_table, type, addr, port, 1);
	return 0;
}

static int cmd_replay_tcp_session(struct session_table *session_table, int ac, char **av)
{
	struct in_addr replay_addr;
	uint16_t replay_port;
	struct in_addr local_addr;
	uint16_t local_port;
	struct in_addr distant_addr;
	uint16_t distant_port;
	int server_mode;
	const struct session_tcp_info *info;
	const struct session_tcp_side *local_side;
	struct replayer replayer;
	int ret = 1;

	replay_addr.s_addr = INADDR_ANY;
	replay_port = 0;
	local_addr.s_addr = INADDR_ANY;
	local_port = 0;
	distant_addr.s_addr = INADDR_ANY;
	distant_port = 0;
	server_mode = 0;
	for (int i = 1 ; i < ac ; i ++) {

		if (strcmp(av[i], "-replay_host") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			if (str2addr_port(av[i + 1], &replay_addr, &replay_port) < 0)
				goto inv_arg;
			i++;
		} else 	if (strcmp(av[i], "-local_host") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			if (str2addr_port(av[i + 1], &local_addr, &local_port) < 0)
				goto inv_arg;
			i++;
		} else if (strcmp(av[i], "-distant_host") == 0) {
			if (i + 1 >= ac)
				goto no_arg;
			if (str2addr_port(av[i + 1], &distant_addr, &distant_port) < 0)
				goto inv_arg;
			i++;
		} else 	if (strcmp(av[i], "-server") == 0)
			server_mode = 1;
		else {
			fprintf(stderr, "Unknown option for <%s> command : <%s>\n", av[0], av[i]);
			goto usage;
		}
		continue;

	inv_arg:
		fprintf(stderr, "Invalid argument for <%s> option\n", av[0]);
		goto usage;
	no_arg:
		fprintf(stderr, "No argument for <%s> option\n", av[0]);
	usage:
		fprintf(stderr, "Usage : %s <-replay_host <addr:port>> [-server] [-local_host <addr:port>] [-distant_host <addr:port>]\n", av[0]);
		return 1;
	}

	if (replay_port == 0 || replay_addr.s_addr == INADDR_ANY) {
		fprintf(stderr, "No host defined\n");
		goto usage;
	}

	if (server_mode && local_port == 0) {
		fprintf(stderr, "No local port defined in server mode\n");
		goto usage;
	}

	if (server_mode == 0 && (distant_addr.s_addr == INADDR_ANY || distant_port == 0)) {
		fprintf(stderr, "No distant host defined in client mode\n");
		goto usage;
	}

	info = session_table_get_tcp(session_table, replay_addr, replay_port, &local_side, NULL);
	if (info == NULL) {
		fprintf(stderr, "Failed to get %s:%d session\n", inet_ntoa(replay_addr), replay_port);
		goto err;
	}

	if (replayer_init(&replayer, server_mode, local_addr, local_port, distant_addr, distant_port, &local_side->tx_list) < 0)
		goto err;

	for (;;) {
		int idle = 0;
		struct timeval now;

		gettimeofday(&now, NULL);

		idle = replayer_loop(&replayer, &now);
		if (idle < 0)
			goto replayer_deinit;

		if (idle)
			usleep(1000);
	}

	ret = 0;

replayer_deinit:
	replayer_deinit(&replayer);
err:
	return ret;
}

static const struct {
	const char *name;
	int(*fun)(struct session_table *session_table, int ac, char **av);
} cmd_table[] = {
	{ "list", cmd_list_session },
	{ "dump", cmd_dump_session },
	{ "replay_tcp", cmd_replay_tcp_session },
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

	if (ac < 2)
		goto usage;

	if (strcmp(av[1], "-") != 0) {
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

	ret = cmd_fun(&session_table, ac - 2, av + 2);

free_session_table_err:
	session_table_free(&session_table);
free_frame_table_err:
	frame_table_free(&frame_table);
close_err:
	pcap_close(pc);
err:
	return ret;

usage:
	fprintf(stderr, "Usage: %s < file.pcap | - > [ cmd [ options ] ]\n", av[0]);
	fprintf(stderr, "cmd:\n");
	for (size_t i = 0 ; i < sizeof cmd_table / sizeof cmd_table[0] ; i ++)
		fprintf(stderr, "%*s%s\n", 4, "", cmd_table[i].name);
	return 1;
}
