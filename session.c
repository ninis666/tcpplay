
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "frame.h"
#include "session.h"

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

	if (pool == NULL) {
		pool = calloc(1, sizeof pool[0]);
		if (pool == NULL) {
			fprintf(stderr, "Failed to create pool : %s\n", strerror(errno));
			goto err;
		}
		*pool_ptr = pool;
	}

	entry = calloc(1, sizeof entry[0]);
	if (entry == NULL) {
		fprintf(stderr, "Failed to create pool entry : %s\n", strerror(errno));
		goto err;
	}
	entry->key = *key;

	entry->prev = pool->session_hash_table[hash].last;
	pool->session_hash_table[hash].last = entry;

err:
	return entry;
}

static int process_udp(struct session_table *table, struct frame *frame)
{
	struct session_entry *entry;
	struct session_key key;
	size_t hash;
	int ret = -1;

	get_key(&key, frame->net.ip.source.s_addr, frame->net.ip.dest.s_addr, frame->proto.udp.hdr.source, frame->proto.udp.hdr.dest);
	hash = get_hash(&key);

	entry = session_table_lookup(table->udp, hash, &key);
	if (entry == NULL) {
		entry = session_table_extend(&table->udp, hash, &key);
		if (entry == NULL)
			goto err;
		printf("%d %d added !\n", htons(frame->proto.udp.hdr.source), htons(frame->proto.udp.hdr.dest));
	} else
		printf("%d %d found back !\n", htons(frame->proto.udp.hdr.source), htons(frame->proto.udp.hdr.dest));

	ret = 1;
err:
	return ret;
}


int session_process_frame(struct session_table *table, struct frame *frame)
{
	int ret = 0;

	if (frame->net.type != frame_net_type_ip)
		goto drop_frame;

	switch (frame->proto.type) {
	default:
		goto drop_frame;

	case frame_proto_type_udp:
		ret = process_udp(table, frame);
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
		free(ptr);
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
