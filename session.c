
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

static struct session_tcp_info *session_tcp_info_alloc(void)
{
	struct session_tcp_info *info;

	info = calloc(1, sizeof info[0]);
	if (info == NULL) {
		fprintf(error_stream, "!!! Failed to create tcp_info : %s\n", strerror(errno));
		goto err;
	}

	return info;

err:
	return NULL;
}

static void session_tcp_info_free(struct session_tcp_info *info)
{
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

static struct session_entry *session_entry_get(struct session_table *table, const uint32_t saddr, const uint32_t daddr, const uint16_t source, const uint16_t dest)
{
	struct session_entry *entry = NULL;
	struct session_key key;
	size_t hash;

	get_key(&key, saddr, daddr, source, dest);
	hash = get_hash(&key);

	entry = session_table_lookup(table->tcp, hash, &key);
	if (entry == NULL)
		entry = session_table_extend(&table->tcp, hash, &key);

	return entry;
}

#define TH_CONNECTED (TH_SYN | TH_ACK)

static int process_tcp(struct session_table *table, struct frame_list *frame_list, struct frame_node *frame_node)
{
	const struct frame *frame = &frame_node->frame;
	const struct tcphdr *hdr = &frame->proto.tcp.hdr;
	const uint32_t seq = htonl(hdr->seq);
	const uint32_t ack = htonl(hdr->ack_seq);
	struct session_entry *entry;
	struct session_tcp_info *info;
	struct session_tcp_side *from;
	struct session_tcp_side *to;

	if (hdr->source == 0 || hdr->dest == 0) {
		fprintf(stderr, "Invalid TCP source / dest = %u / %u\n", hdr->source, hdr->dest);
		goto frame_err;
	}

	entry = session_entry_get(table, frame->net.ip.source.s_addr, frame->net.ip.dest.s_addr, frame->proto.tcp.hdr.source, frame->proto.tcp.hdr.dest);
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

		to->port = hdr->source;
		from->port = hdr->dest;

	} else {

		if (hdr->source == info->side1.port && hdr->dest == info->side2.port) {
			to = &info->side1;
			from = &info->side2;
		}

		if (hdr->source == info->side2.port && hdr->dest == info->side1.port) {
			to = &info->side2;
			from = &info->side1;
		}
	}

	if (to == NULL || from == NULL) {
		fprintf(stderr, "Unexpected TCP source / dest (Got <%u / %u>, <%u / %u> expected\n", hdr->source, hdr->dest, info->side1.port, info->side2.port);
		goto frame_err;
	}

	if ((hdr->th_flags & TH_FIN) != 0) {
		info->status |= TCP_CNX_CLOSED;
		info->status &= ~TCP_CNX_OPEN_DONE;
	}

	if ((info->status & TCP_CNX_CLOSED) != 0)
		goto drop_frame;

	if ((info->status & TCP_CNX_OPEN_DONE) != TCP_CNX_OPEN_DONE) {

		switch (hdr->th_flags) {
		default:
			if (info->server == NULL || info->client == NULL) {
				info->status |= TCP_CNX_OPEN_DONE;
				goto save_frame;
			}

			fprintf(error_stream, "!!! Unexpected flags in CNX stage\n");
			goto frame_err;

		case TH_SYN:
			info->client = from;
			info->server = to;
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
			info->server = from;
			info->client = to;
			from->first_seq = seq;
			break;

		case TH_ACK:
			if (info->server == NULL || info->client == NULL) {
				info->status |= TCP_CNX_OPEN_DONE;
				goto save_frame;
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

save_frame:
	frame_list_unlink(frame_list, frame_node);
	frame_list_link_ordered(&entry->frame_list, frame_node);
	return 1;

frame_err:
	frame_print(error_stream, 0, frame, 0);
drop_frame:
	return 0;
fatal_err:
	return -1;
}

static int process_udp(struct session_table *table, struct frame_list *frame_list, struct frame_node *frame_node)
{
	const struct frame *frame = &frame_node->frame;
	struct session_entry *entry;
	int ret = -1;

	entry = session_entry_get(table, frame->net.ip.source.s_addr, frame->net.ip.dest.s_addr, frame->proto.udp.hdr.source, frame->proto.udp.hdr.dest);
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
		ret = process_udp(table, frame_list, frame_node);
		break;

	case frame_proto_type_tcp:
		ret = process_tcp(table, frame_list, frame_node);
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

static int pool_dump(FILE *file, const int depth, const struct session_pool *pool, const int full)
{
	int done = 0;

	for (size_t idx = 0 ; idx < sizeof pool->session_hash_table / sizeof pool->session_hash_table[0] ; idx ++) {
		for (struct session_entry *entry = pool->session_hash_table[idx].last ; entry != NULL ; entry = entry->prev) {
			done += fprintf(file, "%*sSession %#x:%d <-> %#x:%d\n", depth, "", entry->key.a1, entry->key.p1, entry->key.a2, entry->key.p2);
			done += frame_list_dump(file, depth + 1, &entry->frame_list, full);
		}
	}

	return done;
}

int session_table_dump(FILE *file, const int depth, const struct session_table *table, const int full)
{
	int done = 0;

	if (table->tcp != NULL) {
		done += fprintf(file, "%*sTCP\n", depth, "");
		done += pool_dump(file, depth + 1, table->tcp, full);
	}

	if (table->udp != NULL) {
		done += fprintf(file, "%*sUDP\n", depth, "");
		done += pool_dump(file, depth + 1, table->udp, full);
	}

	return done;
}
