
#ifndef __replayer_h_666__
# define __replayer_h_666__

# include <netinet/in.h>
# include <sys/time.h>

# include "session.h"

# define REPLAYER_FLAGS_SERVER (1 << 0)
# define REPLAYER_FLAGS_CONNECTED (1 << 1)

struct replayer {
	int sock;
	int dist_sock;
	uint8_t flags;
	struct sockaddr_in local;
	struct sockaddr_in distant;
	const struct session_tx_list *tx_list;
	struct timeval first_rx_ts;
	struct timeval last_tx_ts;
	struct session_tx_node *next_tx;
};

int replayer_init(struct replayer *replayer, const int server_mode,
		const struct in_addr local_addr, const uint16_t local_port,
		const struct in_addr distant_addr, const uint16_t distant_port,
		const struct session_tx_list *tx_list);

void replayer_deinit(struct replayer *replayer);

int replayer_loop(struct replayer *replayer, const struct timeval *now);

int replayer_connected(struct replayer *replayer);

#endif
