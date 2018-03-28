
#ifndef __streambuffer_h_666__
# define __streambuffer_h_666__

#include <stdint.h>
#include <stddef.h>

struct streambuffer_data {
	uint8_t *buffer;
	uint8_t *stream;
};

struct streambuffer_node {
	size_t from;
	size_t to;
	struct streambuffer_data data;
	struct streambuffer_node *next;
	struct streambuffer_node *prev;
};

struct streambuffer {
	size_t size;
	struct streambuffer_node *first;
	struct streambuffer_node *last;
};

int streambuffer_init(struct streambuffer *st);
void streambuffer_free(struct streambuffer *list);
int streambuffer_add(struct streambuffer *list, uint8_t *data, const size_t offset, const size_t size);
int streambuffer_dump(FILE *file, const int depth, const struct streambuffer *list);

#endif
