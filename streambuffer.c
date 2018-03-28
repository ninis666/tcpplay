
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "streambuffer.h"
#include "rawprint.h"

int streambuffer_init(struct streambuffer *list)
{
	memset(list, 0, sizeof list[0]);
	return 0;
}

void streambuffer_free(struct streambuffer *list)
{
	struct streambuffer_node *node;

	node = list->first;
	while (node != NULL) {
		struct streambuffer_node *next = node->next;
		free(node->data.buffer);
		free(node);
		node = next;
	}
}

static struct streambuffer_node *node_alloc(uint8_t *data, const size_t data_offset, const size_t from, const size_t to)
{
	struct streambuffer_node *node;

	node = calloc(1, sizeof node[0]);
	if (node == NULL) {
		fprintf(stderr, "Failed to allocate streambuffer_node : %s\n", strerror(errno));
		goto err;
	}

	node->data.buffer = data;
	node->data.stream = data + data_offset;
	node->from = from;
	node->to = to;
err:
	return node;
}

static void node_link_last(struct streambuffer *list, struct streambuffer_node *node)
{
	node->next = NULL;
	node->prev = list->last;
	if (list->last != NULL)
		list->last->next = node;
	else
		list->first = node;
	list->last = node;
	list->size += node->to - node->from + 1;
}

static void node_link_first(struct streambuffer *list, struct streambuffer_node *node)
{
	node->next = list->first;
	node->prev = NULL;

	if (list->first != NULL)
		list->first->prev = node;
	else
		list->last = node;
	list->first = node;
	list->size += node->to - node->from + 1;
}

int streambuffer_add(struct streambuffer *list, uint8_t *data, const size_t offset, const size_t size, struct streambuffer_node **res_ptr)
{
	struct streambuffer_node *node;
	struct streambuffer_node *prev;
	struct streambuffer_node *next;
	const size_t data_from = offset;
	const size_t data_to = offset + size - 1;
	int hole;

	/*
	 * prev will point to the node brefore or containing this data
	 */
	prev = list->last;
	while (prev != NULL) {
		if (data_from >= prev->from)
			break;
		prev = prev->prev;
	}

	if (prev == NULL) {
		/*
		 * This data comes before all known one, make it first
		 */
		node = node_alloc(data, 0, data_from, data_to);
		if (node == NULL)
			goto err;
		node_link_first(list, node);
		goto saved;
	}

	/*
	 * next will point to the node after or containing this data
	 */
	hole = 0;
	next = prev->next;
	while (next != NULL) {
		if (next->prev->to + 1 != next->from) {
			hole ++;
			break;
		}
		if (next->to >= data_to)
			break;
		next = next->next;
	}

	if (!hole && next != NULL)
		goto already_saved; /* There is no hole, and next contains this data */

	if (next == NULL) {
		/*
		 * This data comes after all known one, make it first
		 */
		node = node_alloc(data, 0, data_from, data_to);
		if (node == NULL)
			goto err;
		node_link_last(list, node);
		goto saved;
	}

	/*
	 * TODO:
	 * holes and non contiguous data are not managed for now !
	 */
	goto err;

saved:
	if (res_ptr != NULL)
		*res_ptr = node;
	return 1;
already_saved:
	if (res_ptr != NULL)
		*res_ptr = prev;
	return 0;
err:
	return -1;
}

int streambuffer_node_dump(FILE *file, const int depth, const struct streambuffer_node *node)
{
	int done = 0;
	done += fprintf(file, "%*s[%zd - %zd]\n", depth, "", node->from, node->to);
	done += rawprint(file, depth, node->data.stream, node->to - node->from + 1, 8, 4);
	return done;
}

int streambuffer_dump(FILE *file, const int depth, const struct streambuffer *list)
{
	int done = 0;

	for (const struct streambuffer_node *node = list->first ; node != NULL ; node = node->next)
		done += streambuffer_node_dump(file, depth, node);

	return done;
}
