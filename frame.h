
#ifndef __frame_h_666__
# define __frame_h_666__

# include <net/ethernet.h>
# include <netinet/udp.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <stdio.h>

struct frame_hw {
	uint8_t  source[ETH_ALEN]; /* source ether addr	*/
	uint8_t  dest[ETH_ALEN];   /* destination eth addr	*/
};

struct frame_net_ip {
	struct in_addr source;	/* source IP addr	*/
	struct in_addr dest;	/* destination IP addr	*/
};

struct frame_net_arp {
	uint16_t opcode;
	uint8_t  hw_source[ETH_ALEN]; /* source ether addr	*/
	uint8_t  hw_dest[ETH_ALEN];   /* destination eth addr	*/
	struct in_addr ip_source;    /* source IP addr	*/
	struct in_addr ip_dest;      /* destination IP addr	*/
};

enum frame_net_type {
	frame_net_type_arp,
	frame_net_type_ip,
};

struct frame_net {
	enum frame_net_type type;
	union {
		struct frame_net_ip ip;
		struct frame_net_arp arp;
	};
};

struct frame_proto_udp {
	struct udphdr hdr;
	uint32_t data_size;
	uint8_t *data;
};

struct frame_proto_tcp {
	struct tcphdr hdr;

	uint16_t opt_size;
	uint8_t *opt;

	uint32_t app_data_size;
	uint8_t *app_data;
};

enum frame_proto_type {
	frame_proto_type_udp,
	frame_proto_type_tcp,
};

struct frame_proto {
	enum frame_proto_type type;
	union {
		struct frame_proto_udp udp;
		struct frame_proto_tcp tcp;
	};
};

struct frame {
	struct frame_hw hw;
	struct frame_net net;
	struct frame_proto proto;
};

int frame_print_hw(FILE *file, const int depth, const struct frame_hw *hw);
int frame_print_net(FILE *file, const int depth, const struct frame_net *net);
int frame_print_proto(FILE *file, const int depth, const struct frame_proto *proto);

#endif
