
#ifndef __decode_h_666__
# define __decode_h_666__

# include <stdint.h>
# include "frame.h"

typedef int (*decode_fun_t)(struct frame *frame, const int depth, const void *data, const uint32_t len, void *private);
decode_fun_t decode_get(const char *from, const int type);

#endif

