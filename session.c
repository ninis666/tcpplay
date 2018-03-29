
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "frame.h"
#include "session.h"

#define error_stream stderr

int session_table_init(struct session_table *table)
{
	memset(table, 0, sizeof table[0]);
	return 0;
}

static inline uint32_t MurmurHash_32(const void *key, uint32_t len, const uint32_t seed)
{
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	// Initialize the hash to a 'random' value

	uint32_t h = seed ^ len;

	// Mix 4 bytes at a time into the hash

	const uint8_t *data = key;

	while (len >= 4) {
		uint32_t k = *(uint32_t *)data;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	// Handle the last few bytes of the input array

	switch (len) {
	case 3:
		h ^= data[2] << 16; // fall through
	case 2:
		h ^= data[1] << 8; // fall through
	case 1:
		h ^= data[0];
		h *= m;
	};

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

static int tx_list_node_add(struct session_tx_list *list, const struct timeval *ts, const struct streambuffer_node *buffer)
{
	struct session_tx_node *after;
	struct session_tx_node *node;

	for (after = list->last ; after != NULL ; after = after->prev) {
		if (timercmp(ts, &after->tx.ts, >))
			break;
		if (!timercmp(ts, &after->tx.ts, <))
			break;
	}

	node = calloc(1, sizeof node[0]);
	if (node == NULL) {
		fprintf(stderr, "Failed to allocate tx_node : %s\n", strerror(errno));
		goto err;
	}
	node->tx.ts = *ts;
	node->tx.buffer = buffer;

	if (after != NULL) {

		node->prev = after;
		node->next = after->next;

		if (after->next != NULL)
			after->next->prev = node;
		else
			list->last = node;
		after->next = node;

	} else {

		node->prev = NULL;
		node->next = list->first;

		if (list->first != NULL)
			list->first->prev = node;
		else
			list->last = node;
		list->first = node;
	}

	return 0;

err:
	return -1;

}

static int tx_list_init(struct session_tx_list *list)
{
	memset(list, 0, sizeof list[0]);
	return 0;
}

static void tx_list_free(struct session_tx_list *list)
{
	struct session_tx_node *node;

	node = list->first;
	while (node != NULL) {
		struct session_tx_node *next = node->next;
		free(node);
		node = next;
	}
}

static struct session_tcp_info *session_tcp_info_alloc(void)
{
	struct session_tcp_info *info;

	info = calloc(1, sizeof info[0]);
	if (info == NULL) {
		fprintf(error_stream, "!!! Failed to create tcp_info : %s\n", strerror(errno));
		goto err;
	}

	if (streambuffer_init(&info->side1.tx_buffer) < 0)
		goto free_err;

	if (streambuffer_init(&info->side2.tx_buffer) < 0)
		goto free_stream1_err;

	if (tx_list_init(&info->side1.tx_list) < 0)
		goto free_stream2_err;

	if (tx_list_init(&info->side2.tx_list) < 0)
		goto free_tx1_err;

	return info;

free_tx1_err:
	tx_list_free(&info->side1.tx_list);
free_stream2_err:
	streambuffer_free(&info->side2.tx_buffer);
free_stream1_err:
	streambuffer_free(&info->side1.tx_buffer);
free_err:
	free(info);
err:
	return NULL;
}

static void session_tcp_info_free(struct session_tcp_info *info)
{
	streambuffer_free(&info->side2.tx_buffer);
	streambuffer_free(&info->side1.tx_buffer);
	tx_list_free(&info->side1.tx_list);
	tx_list_free(&info->side2.tx_list);
	free(info);
}

static struct session_entry *session_entry_alloc(const struct session_key *key)
{
	struct session_entry *entry;

	entry = calloc(1, sizeof entry[0]);
	if (entry == NULL) {
		fprintf(error_stream, "!!! Failed to create pool entry : %s\n", strerror(errno));
		goto err;
	}
	if (frame_list_init(&entry->frame_list) < 0)
		goto free_entry_err;
	entry->key = *key;
	return entry;

free_entry_err:
	free(entry);
err:
	return NULL;
}

static void session_entry_free(struct session_entry *entry)
{
	if (entry->tcp_info != NULL)
		session_tcp_info_free(entry->tcp_info);
	frame_list_free(&entry->frame_list);
	free(entry);
}

static inline void get_key(struct session_key *key, const uint32_t a1, const uint32_t a2, const uint16_t p1, const uint16_t p2)
{
	key->a1 = a1 < a2 ? a1 : a2;
	key->a2 = a1 < a2 ? a2 : a1;
	key->p1 = p1 < p2 ? p1 : p2;
	key->p2 = p1 < p2 ? p2 : p1;
}

static inline size_t get_hash(const struct session_key *key)
{
	const struct session_pool *null_pool = NULL;
	return MurmurHash_32(key, sizeof key[0], 0) % (sizeof null_pool->session_hash_table / sizeof null_pool->session_hash_table[0]);
}

static struct session_entry *session_table_lookup(const struct session_pool *pool, const size_t hash, const struct session_key *key)
{
	struct session_entry *entry = NULL;

	if (pool != NULL) {

		for (entry = pool->session_hash_table[hash].last ; entry != NULL ; entry = entry->prev) {
			if (memcmp(&entry->key, key, sizeof key[0]) == 0)
				break;
		}
	}

	return entry;
}

static struct session_entry *session_table_extend(struct session_pool **pool_ptr, const size_t hash, const struct session_key *key)
{
	struct session_pool *pool = *pool_ptr;
	struct session_entry *entry = NULL;
	struct session_pool *new_pool = NULL;

	if (pool == NULL) {
		pool = calloc(1, sizeof pool[0]);
		if (pool == NULL) {
			fprintf(error_stream, "!!! Failed to create pool : %s\n", strerror(errno));
			goto err;
		}
		*pool_ptr = pool;
		new_pool = pool;
	}

	entry = session_entry_alloc(key);
	if (entry == NULL)
		goto err;

	entry->prev = pool->session_hash_table[hash].last;
	pool->session_hash_table[hash].last = entry;
	if (new_pool != NULL)
		*pool_ptr = new_pool;
	return entry;

err:
	if (new_pool != NULL)
		free(new_pool);
	return NULL;
}

static struct session_entry *session_entry_get(struct session_pool **pool_ptr, const uint32_t saddr, const uint32_t daddr, const uint16_t source, const uint16_t dest)
{
	struct session_entry *entry = NULL;
	struct session_key key;
	struct session_pool *pool = *pool_ptr;
	size_t hash;

	get_key(&key, saddr, daddr, source, dest);
	hash = get_hash(&key);

	if (pool == NULL)
		entry = NULL;
	else
		entry = session_table_lookup(pool, hash, &key);

	if (entry == NULL)
		entry = session_table_extend(pool_ptr, hash, &key);

	return entry;
}

#define TH_CONNECTED (TH_SYN | TH_ACK)

static int process_tcp(struct session_pool **pool_ptr, struct frame_node *frame_node)
{
	struct frame *frame = &frame_node->frame;
	const struct frame_net_ip *ip = &frame->net.ip;
	const struct tcphdr *tcp = &frame->proto.tcp.hdr;
	const uint32_t seq = htonl(tcp->seq);
	const uint32_t ack = htonl(tcp->ack_seq);
	struct session_entry *entry;
	struct session_tcp_info *info;
	struct session_tcp_side *from;
	struct session_tcp_side *to;
	size_t len;
	uint8_t *data;
	size_t offset;

	if (tcp->source == 0 || tcp->dest == 0) {
		fprintf(stderr, "Invalid TCP source / dest = %u / %u\n", tcp->source, tcp->dest);
		goto frame_err;
	}

	entry = session_entry_get(pool_ptr, ip->source.s_addr, ip->dest.s_addr, tcp->source, tcp->dest);
	if (entry == NULL)
		goto fatal_err;

	from = NULL;
	to = NULL;

	info = entry->tcp_info;
	if (info == NULL) {
		info = session_tcp_info_alloc();
		if (info == NULL)
			goto fatal_err;
		entry->tcp_info = info;

		to = &info->side1;
		from = &info->side2;

		to->addr = ip->source;
		to->port = tcp->source;
		from->addr = ip->dest;
		from->port = tcp->dest;

	} else {

		if (ip->source.s_addr == info->side1.addr.s_addr && ip->dest.s_addr == info->side2.addr.s_addr && tcp->source == info->side1.port && tcp->dest == info->side2.port) {
			to = &info->side1;
			from = &info->side2;
		}

		if (ip->source.s_addr == info->side2.addr.s_addr && ip->dest.s_addr == info->side1.addr.s_addr && tcp->source == info->side2.port && tcp->dest == info->side1.port) {
			to = &info->side2;
			from = &info->side1;
		}
	}

	if (to == NULL || from == NULL) {
		fprintf(stderr, "Unexpected source / dest (Got <%u / %u>, <%u / %u> expected\n", tcp->source, tcp->dest, info->side1.port, info->side2.port);
		goto frame_err;
	}

	if ((tcp->th_flags & TH_FIN) != 0) {
		info->status |= TCP_CNX_CLOSED;
		info->status &= ~TCP_CNX_OPEN_DONE;
	}

	if ((info->status & TCP_CNX_CLOSED) != 0)
		goto drop_frame;

	if ((info->status & TCP_CNX_OPEN_DONE) != TCP_CNX_OPEN_DONE) {

		switch (tcp->th_flags) {
		default:
			if (info->client == NULL || info->server == NULL) {
				info->status |= TCP_CNX_OPEN_DONE;
				from->first_seq = seq - 1;
				to->first_seq = ack - 1;
				goto keep_frame;
			}

			fprintf(error_stream, "!!! Unexpected flags in CNX stage\n");
			goto frame_err;

		case TH_SYN:
			info->client = to;
			info->server = from;
			info->status = TCP_CNX_SYN;
			from->first_seq = seq;
			to->seq = 0;
			break;

		case TH_SYN | TH_ACK:
			if (ack != to->first_seq + 1) {
				fprintf(error_stream, "!!! Invalid SYN ACK on cnx : Got <%u>, <%u> expected\n", ack, to->first_seq + 1);
				goto frame_err;
			}

			info->status |= TCP_CNX_SYN_ACK;
			info->client = from;
			info->server = to;
			from->first_seq = seq;
			break;

		case TH_ACK:
			if (info->client == NULL || info->server == NULL) {
				info->status |= TCP_CNX_OPEN_DONE;
				from->first_seq = seq - 1;
				to->first_seq = ack - 1;
				goto keep_frame;
			}

			if (ack != to->first_seq + 1) {
				fprintf(error_stream, "!!! Invalid ACK on cnx : Got <%u>, <%u> expected\n", ack, to->first_seq + 1);
				goto frame_err;
			}
			info->status |= TCP_CNX_OPEN_DONE;
			break;
		}

		goto drop_frame;
	}

keep_frame:

	offset = seq - from->first_seq - 1;

	len = frame_steal_app(frame, &data);
	if (len > 0) {
		int res;
		struct streambuffer_node *buffer = NULL;

		res = streambuffer_add(&to->tx_buffer, data, offset, len, &buffer);
		if (res <= 0)
			frame_update_app(frame, data, len);
		else
			tx_list_node_add(&to->tx_list, &frame->ts, buffer);

		if (res < 0) {
			fprintf(stderr, "!!! TCP data have not been saved (offset = %zd)\n", offset);
			goto frame_err;
		}

	}
	return 0;

frame_err:
	frame_print(error_stream, 0, frame, 0);
drop_frame:
	return 0;
fatal_err:
	return -1;
}

static int process_udp(struct session_pool **pool_ptr, struct frame_list *frame_list, struct frame_node *frame_node)
{
	const struct frame *frame = &frame_node->frame;
	struct session_entry *entry;
	int ret = -1;

	entry = session_entry_get(pool_ptr, frame->net.ip.source.s_addr, frame->net.ip.dest.s_addr, frame->proto.udp.hdr.source, frame->proto.udp.hdr.dest);
	if (entry == NULL)
		goto err;

	frame_list_unlink(frame_list, frame_node);
	frame_list_link_ordered(&entry->frame_list, frame_node);
	ret = 1;
err:
	return ret;
}

int session_process_frame(struct session_table *table, struct frame_list *frame_list, struct frame_node *frame_node)
{
	int ret = 0;
	struct frame *frame = &frame_node->frame;

	if (frame->net.type != frame_net_type_ip)
		goto drop_frame;

	switch (frame->proto.type) {
	default:
		goto drop_frame;

	case frame_proto_type_udp:
		ret = process_udp(&table->udp, frame_list, frame_node);
		break;

	case frame_proto_type_tcp:
		ret = process_tcp(&table->tcp, frame_node);
		break;

	}

drop_frame:
	return ret;
}

static void session_entry_list_free(struct session_entry *last)
{
	struct session_entry *ptr;
	struct session_entry *prev;

	ptr = last;
	while (ptr != NULL) {
		prev = ptr->prev;
		session_entry_free(ptr);
		ptr = prev;
	}
}

static void session_pool_free(struct session_pool *pool)
{
	size_t idx;

	for (idx = 0 ; idx < sizeof pool->session_hash_table / sizeof pool->session_hash_table[0] ; idx ++)
		session_entry_list_free(pool->session_hash_table[idx].last);
	free(pool);
}

void session_table_free(struct session_table *table)
{
	if (table->tcp != NULL) {
		session_pool_free(table->tcp);
		table->tcp = NULL;
	}

	if (table->udp != NULL) {
		session_pool_free(table->udp);
		table->udp = NULL;
	}
}

static int generic_pool_dump(FILE *file, const int depth, const struct session_pool *pool, const int full)
{
	int done = 0;

	for (size_t idx = 0 ; idx < sizeof pool->session_hash_table / sizeof pool->session_hash_table[0] ; idx ++) {
		for (struct session_entry *entry = pool->session_hash_table[idx].last ; entry != NULL ; entry = entry->prev) {
			done += fprintf(file, "%*sSession %#x-%#x-%#x-%#x\n", depth, "", entry->key.a1, entry->key.p1, entry->key.a2, entry->key.p2);
			if (full > 0)
				done += frame_list_dump(file, depth + 1, &entry->frame_list, full);
		}
	}

	return done;
}


static int tcp_side_dump(FILE *file, const int depth, const char *name, const struct session_tcp_side *side, const struct timeval *t0, const int full)
{
	int done = 0;

	done += fprintf(file, "%*s%s : %s:%d\n", depth, "", name, inet_ntoa(side->addr), htons(side->port));

	if (full > 0) {

		for (struct session_tx_node *node = side->tx_list.first ; node != NULL ; node = node->next) {
			struct timeval dt;

			timersub(&node->tx.ts, t0, &dt);

			done += fprintf(file, "%*s[%ld, %ld]\n", depth, "", dt.tv_sec, dt.tv_usec);
			done += streambuffer_node_dump(file, depth + 1, node->tx.buffer);
		}
	}

	return done;
}

static int tcp_info_dump(FILE *file, const int depth, const struct session_tcp_info *info, const int full)
{
	int done = 0;
	const struct session_tcp_side *side1;
	const char *side1_name;
	const struct session_tcp_side *side2;
	const char *side2_name;
	struct timeval t1, t2, *t0;

	if (info->client != NULL || info->server != NULL) {
		if (info->client == NULL || info->server == NULL)
			abort();
		side1 = info->client;
		side1_name = "client";
		side2 = info->server;
		side2_name = "server";
	} else {
		side1 = &info->side1;
		side1_name = "side1";
		side2 = &info->side2;
		side2_name = "side2";
	}


	if (side1->tx_list.first != NULL)
		t1 = side1->tx_list.first->tx.ts;
	else
		memset(&t1, 0, sizeof t1);
	if (side2->tx_list.first != NULL)
		t2 = side2->tx_list.first->tx.ts;
	else
		memset(&t2, 0, sizeof t2);
	if (timercmp(&t1, &t2, <))
		t0 = &t1;
	else
		t0 = &t2;

 	done += tcp_side_dump(file, depth, side1_name, side1, t0, full);
 	done += tcp_side_dump(file, depth, side2_name, side2, t0, full);
	return done;
}

static int tcp_pool_dump(FILE *file, const int depth, const struct session_pool *pool, const int full)
{
	int done = 0;

	for (size_t idx = 0 ; idx < sizeof pool->session_hash_table / sizeof pool->session_hash_table[0] ; idx ++) {
		for (const struct session_entry *entry = pool->session_hash_table[idx].last ; entry != NULL ; entry = entry->prev) {
			const struct session_tcp_info *info = entry->tcp_info;

			if (info == NULL)
				abort();

			done += fprintf(file, "%*sSession %#x-%#x-%#x-%#x\n", depth, "", entry->key.a1, entry->key.p1, entry->key.a2, entry->key.p2);
			done += tcp_info_dump(file, depth + 1, info, full);
		}
	}

	return done;
}

int session_table_dump(FILE *file, const int depth, const struct session_table *table, const int full)
{
	int done = 0;

	if (table->tcp != NULL) {
		done += fprintf(file, "%*sTCP\n", depth, "");
		done += tcp_pool_dump(file, depth + 1, table->tcp, full);
	}

	if (table->udp != NULL) {
		done += fprintf(file, "%*sUDP\n", depth, "");
		done += generic_pool_dump(file, depth + 1, table->udp, full);
	}

	return done;
}
