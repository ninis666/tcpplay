
#ifndef __session_h_666__
# define __session_h_666__

# include <stdint.h>
# include <netinet/tcp.h>
# include "frame_list.h"
# include "streambuffer.h"

# define SESSION_HASH_SIZE 1021

#define session_tcp_cnx_done (1 << 0)

#define TCP_CNX_SYN     (1 << 0)
#define TCP_CNX_SYN_ACK (1 << 1)
#define TCP_CNX_ACK     (1 << 2)
#define TCP_CNX_FIN     (1 << 3)

#define TCP_CNX_OPEN_DONE (TCP_CNX_SYN | TCP_CNX_SYN_ACK | TCP_CNX_ACK)
#define TCP_CNX_CLOSED (TCP_CNX_FIN)

struct session_key {
	uint32_t a1;
	uint32_t a2;
	uint16_t p1;
	uint16_t p2;
};

struct session_tcp_side {
	uint32_t first_seq;
	struct in_addr addr;
	uint16_t port;
	uint32_t seq;
	struct streambuffer tx_buffer;
};

struct session_tcp_info {
	uint8_t status;
	struct session_tcp_side side1;
	struct session_tcp_side side2;
	struct session_tcp_side *server;
	struct session_tcp_side *client;
};

struct session_entry {
	struct session_key key;
	struct session_entry *prev;

	struct session_tcp_info *tcp_info;
	struct frame_list frame_list;
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
int session_table_dump(FILE *file, const int depth, const struct session_table *table, const int full);

#endif
