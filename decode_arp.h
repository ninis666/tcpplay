
#ifndef __decode_arp_h_666__
# define __decode_arp_h_666__

# include "frame.h"

int decode_arp(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private);

#endif

