
#ifndef __session_h_666__
# define __session_h_666__

#include <stdint.h>
#include "frame_list.h"

#define SESSION_HASH_SIZE 1021

struct session_key {
	uint32_t a1;
	uint32_t a2;
	uint16_t p1;
	uint16_t p2;
};

struct session_entry {
	struct session_key key;
	struct frame_list frame_list;
	struct session_entry *prev;
};

struct session_pool {
	struct {
		struct session_entry *last;
	}  session_hash_table[SESSION_HASH_SIZE];
};

struct session_table {
	struct session_pool *tcp;
	struct session_pool *udp;
};

int session_table_init(struct session_table *table);
void session_table_free(struct session_table *table);

int session_process_frame(struct session_table *table, struct frame_list *fame_list, struct frame_node *frame_node);
int session_table_dump(FILE *file, const int depth, struct session_table *table);

#endif
