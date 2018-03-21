
#ifndef __frame_list_h_666__
# define __frame_list_h_666__

# include "frame.h"

struct frame_node {
	struct frame frame;
	struct frame_node *next;
	struct frame_node *prev;
};

struct frame_list {
	struct frame_node *first;
	struct frame_node *last;
	size_t count;
};

struct frame_table {
	struct frame_list free_list;
	struct frame_list used_list;
};

int frame_table_init(struct frame_table *table);
void frame_table_free(struct frame_table *table);
struct frame_node *frame_node_new(struct frame_table *table, const struct timeval *ts);
void frame_node_free(struct frame_table *table, struct frame_node *node);

#endif
