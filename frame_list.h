
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

typedef int (*frame_node_cmp_fun_t)(const struct frame_node *node1, const struct frame_node *node2);

int frame_table_init(struct frame_table *table);
void frame_table_free(struct frame_table *table);
struct frame_node *frame_node_new(struct frame_table *table, const struct timeval *ts);
void frame_node_recycle(struct frame_table *table, struct frame_node *node);

int frame_list_init(struct frame_list *list);
void frame_list_free(struct frame_list *list);

void frame_list_unlink(struct frame_list *list, struct frame_node *node);

void frame_list_link_ordered_ext(struct frame_list *list, struct frame_node *node, frame_node_cmp_fun_t cmd_fun);
void frame_list_link_ordered(struct frame_list *list, struct frame_node *node);

int frame_list_dump(FILE *file, const int depth, const struct frame_list *list, const int full);

#endif
