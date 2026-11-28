# SPDX-License-Identifier: GPL-2.0-only
CC ?= cc
CFLAGS ?= -O2
LDLIBS += -pthread

.PHONY: all clean
all: ktls-bench

ktls-bench: tls_rx_bench.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Wformat=2 \
		-pthread $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) ktls-bench
