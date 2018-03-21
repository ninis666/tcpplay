
#include <stdio.h>
#include <pcap/pcap.h>

#include "decode.h"
#include "decode_sll.h"
#include "decode_eth.h"

decode_fun_t decode_get(const char *from, const int type)
{
	decode_fun_t ret = NULL;

	switch (type) {
	default:
		if (from != NULL)
			fprintf(stderr, "Unexpected datalink from <%s> : %s (%d, %s)\n", from, pcap_datalink_val_to_description(type), type, pcap_datalink_val_to_name(type));
		break;

	case DLT_LINUX_SLL:
		ret = decode_sll;
		break;

	case DLT_EN10MB:
		ret = decode_eth;
		break;
	}

	return ret;
}
