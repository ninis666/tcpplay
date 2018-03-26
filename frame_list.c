
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "frame_list.h"

enum {
	pool_inc = 16,
};

int frame_list_init(struct frame_list *list)
{
	memset(list, 0, sizeof list[0]);
	return 0;
}

static void list_link_first(struct frame_list *list, struct frame_node *node)
{
	node->prev = NULL;
	node->next = list->first;

	if (list->first != NULL)
		list->first->prev = node;
	else
		list->last = node;
	list->first = node;
	list->count ++;
	if (list->count == 0)
		abort();
}

static void list_link_last(struct frame_list *list, struct frame_node *node)
{
	node->prev = list->last;
	node->next = NULL;

	if (list->last != NULL)
		list->last->next = node;
	else
		list->first = node;
	list->last = node;
	list->count ++;
	if (list->count == 0)
		abort();
}

static void list_link_after(struct frame_list *list, struct frame_node *after, struct frame_node *node)
{
	node->prev = after;
	node->next = after->next;

	if (after->next != NULL)
		after->next->prev = node;
	else
		list->last = node;
	after->next = node;
	list->count ++;
	if (list->count == 0)
		abort();
}

void frame_list_unlink(struct frame_list *list, struct frame_node *node)
{
	if (list->count == 0)
		abort();

	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		list->first = node->next;

	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		list->last = node->prev;
	node->next = NULL;
	node->prev = NULL;
	list->count --;
}

void frame_list_link_ordered(struct frame_list *list, struct frame_node *node)
{
	struct frame_node *node_after;

	for (node_after = list->last ; node_after != NULL ; node_after = node_after->prev) {
		if (timercmp(&node->frame.ts, &node_after->frame.ts, >))
			break;
	}

	if (node_after != NULL)
		list_link_after(list, node_after, node);
	else
		list_link_first(list, node);
}

void frame_list_free(struct frame_list *list)
{
	struct frame_node *node;

	node = list->first;
	while (node != NULL) {
		struct frame_node *next = node->next;

		if (list->count == 0)
			abort();

		frame_deinit(&node->frame);
		free(node);
		node = next;

		list->count --;
	}
	if (list->count != 0)
		abort();
}

int frame_table_init(struct frame_table *table)
{
	memset(table, 0, sizeof table[0]);

	if (frame_list_init(&table->free_list) < 0)
		goto err;

	if (frame_list_init(&table->used_list) < 0)
		goto free_free_list_err;
	return 0;

free_free_list_err:
	frame_list_free(&table->free_list);
err:
	return -1;
}

void frame_node_recycle(struct frame_table *table, struct frame_node *node)
{
	/*
	 * Move this node from used_list to free_list
	 */
	frame_list_unlink(&table->used_list, node);
	list_link_last(&table->free_list, node);
	frame_deinit(&node->frame);
}

void frame_table_free(struct frame_table *table)
{

	frame_list_free(&table->used_list);
	frame_list_free(&table->free_list);
	memset(table, 0, sizeof table[0]);
}

struct frame_node *frame_node_new(struct frame_table *table, const struct timeval *ts)
{
	struct frame_node *node;

	if (table->free_list.first == NULL) {

		for (size_t i = 0 ; i < pool_inc ; i++) {
			node = calloc(1, sizeof node[0]);
			if (node == NULL) {
				fprintf(stderr, "Failed to extend pool : %s\n", strerror(errno));
				if (i == 0) {
					fprintf(stderr, "Cannot add a single new frame\n");
					goto err;
				}
				break;
			}
			list_link_last(&table->free_list, node);
		}
	}

	/*
	 * Get the first node from free_list
	 */
	node = table->free_list.first;
	if (node == NULL)
		abort();
	if (frame_init(&node->frame, ts) < 0)
		goto err;
	frame_list_unlink(&table->free_list, node);
	frame_list_link_ordered(&table->used_list, node);
	return node;

err:
	return NULL;

}
