
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "rawprint.h"

int frame_print_hw(FILE *file, const int depth, const struct frame_hw *hw)
{
	size_t i;
	int done = 0;

	done += fprintf(file, "%*sHW ", depth, "");
	for (i = 0 ; i < sizeof hw->source ; i++)
		done += fprintf(file, "%02x%s", hw->source[i], i + 1 >= sizeof hw->source ? "->" : ":");

	for (i = 0 ; i < sizeof hw->dest ; i++)
		done += fprintf(file, "%02x%s", hw->dest[i], i + 1 >= sizeof hw->dest ? "\n" : ":");

	return done;
}

static int print_net_ip(FILE *file, const int depth, const struct frame_net_ip *ip)
{
	int done = 0;

	done += fprintf(file, "%*sIP %s -> ", depth, "", inet_ntoa(ip->source));
	done += fprintf(file, "%s\n", inet_ntoa(ip->dest));

	return done;
}

static int print_net_arp(FILE *file, const int depth, const struct frame_net_arp *arp)
{
	size_t i;
	int done = 0;

	done += fprintf(file, "%*sARP OPCODE %d ", depth, "", arp->opcode);

	for (i = 0 ; i < sizeof arp->hw_source ; i++)
		done += fprintf(file, "%02x%s", arp->hw_source[i], i + 1 >= sizeof arp->hw_source ? "->" : ":");

	for (i = 0 ; i < sizeof arp->hw_dest ; i++)
		done += fprintf(file, "%02x%s", arp->hw_dest[i], i + 1 >= sizeof arp->hw_dest ? " " : ":");

	done += fprintf(file, "%s -> ", inet_ntoa(arp->ip_source));
	done += fprintf(file, "%s\n", inet_ntoa(arp->ip_dest));

	return done;
}


int frame_print_net(FILE *file, const int depth, const struct frame_net *net)
{
	int done = 0;

	switch (net->type) {
	default:
		return fprintf(file, "%*sNo net layer\n", depth, "");

	case frame_net_type_arp:
		return print_net_arp(file, depth, &net->arp);

	case frame_net_type_ip:
		return print_net_ip(file, depth, &net->ip);
	}

	return done;

}

int frame_print_app(FILE *file, const int depth, const struct frame_app *app)
{
	int done = 0;
	done += fprintf(file, "%*sData (%db)\n", depth, "", app->size);
	if (app->size > 0)
		done += rawprint(file, depth + 1, app->data, app->size, 8, 4);
	return done;
}

static int print_proto_udp(FILE *file, const int depth, const struct frame_proto_udp *udp)
{
	int done = 0;
	done += fprintf(file, "%*sUDP %d -> %d\n", depth, "", htons(udp->hdr.source), htons(udp->hdr.dest));
	return done;
}

static int print_proto_tcp(FILE *file, const int depth, const struct frame_proto_tcp *tcp)
{
	int done = 0;
	const struct tcphdr *hdr = &tcp->hdr;

	done += fprintf(file, "%*sTCP %d -> %d\n", depth, "", htons(hdr->source), htons(hdr->dest));
	done += fprintf(file, "%*sseq = %d\n", depth + 1, "", htons(hdr->seq));
	done += fprintf(file, "%*sack_seq = %d\n", depth + 1, "", htons(hdr->ack_seq));
	done += fprintf(file, "%*shdr_len = %d (%db)\n", depth + 1, "", hdr->doff, hdr->doff * 4);
	done += fprintf(file, "%*sflags = [ %s%s%s%s%s%s]\n", depth + 1, "", hdr->urg ? "URG " : "", hdr->syn ? "SYN " : "", hdr->ack ? "ACK " : "", hdr->psh ? "PSH " : "", hdr->rst ? "RST " : "", hdr->fin ? "FIN " : "");
	done += fprintf(file, "%*swindow = %d\n", depth + 1, "", hdr->window);

	for (uint16_t idx = 0 ; idx < tcp->opt_size ; ) {
		uint8_t opt_val_len;

		switch (tcp->opt[idx]) {
		case 0:
			/* End */
			idx = tcp->opt_size;
			break;

		case 1:
			/* NOP */
			idx ++;
			break;

		default:
			opt_val_len = tcp->opt[idx + 1];
			if (opt_val_len == 0) {
				fprintf(stderr, "Invalid TCP option len");
				break;
			}

			done += fprintf(file, "%*sOPCODE  = %#02x (%db)\n", depth + 1, "", tcp->opt[idx], opt_val_len);
			rawprint(stdout, depth + 2, tcp->opt + idx, opt_val_len, 4, 1);
			idx += opt_val_len;
			break;
		}
	}

	return done;
}

int frame_print_proto(FILE *file, const int depth, const struct frame_proto *proto)
{
	switch (proto->type) {
	default:
		return fprintf(file, "%*sNo proto layer\n", depth, "");

	case frame_proto_type_udp:
		return print_proto_udp(file, depth, &proto->udp);

	case frame_proto_type_tcp:
		return print_proto_tcp(file, depth, &proto->tcp);
	}
}

int frame_print(FILE *file, const int depth, const struct frame *frame)
{
	int done = 0;

	done += frame_print_hw(file, depth, &frame->hw);
	done += frame_print_net(file, depth + 1, &frame->net);
	done += frame_print_proto(file, depth + 2, &frame->proto);
	done += frame_print_app(file, depth + 3, &frame->app);

	return done;
}

int frame_init(struct frame *frame)
{
	memset(frame, 0, sizeof frame[0]);
	return 0;
}

void frame_deinit(struct frame *frame)
{
	if (frame->proto.type == frame_proto_type_tcp)
		free(frame->proto.tcp.opt);
	free(frame->app.data);
	frame_init(frame);
}
