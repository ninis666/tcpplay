
DEP_FILE = .$(shell pwd | sed 's|/||g').depend
EXE = tcpplay tcp_server tcp_client generate
CFLAGS = -g -Wall -Wextra -Werror

all : $(DEP_FILE) $(EXE)

tcpplay: tcpplay.o \
	decode_eth.o \
	decode_sll.o \
	decode.o \
	decode_arp.o \
	decode_ip.o \
	decode_tcp.o \
	decode_udp.o \
	rawprint.o \
	frame.o \
	frame_list.o \
	session.o \
	streambuffer.o \
	replayer.o
	$(CC) $(LDFLAGS) $^ -lpcap -o $@

tcp_server: tcp_server.o
	$(CC) $(LDFLAGS) $^ -lpthread -o $@

tcp_client: tcp_client.o
	$(CC) $(LDFLAGS) $^ -o $@

generate: generate.o
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o *~ $(EXE) $(DEP_FILE) core.* vgcore.*

%.o: %.c Makefile $(DEP_FILE)
	$(CC) $(CFLAGS) -c $< -o $@

$(DEP_FILE) depend dep: Makefile
	$(CC) -MM -MG $(CFLAGS) *.c > $(DEP_FILE)

ifeq ($(DEP_FILE),$(wildcard $(DEP_FILE)))
include $(DEP_FILE)
endif
