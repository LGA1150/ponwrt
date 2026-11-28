# SPDX-License-Identifier: GPL-2.0-only
CC ?= cc
CFLAGS ?= -O2
LDLIBS += -lssl -lcrypto -pthread

.PHONY: all clean check
all: tls-proxy-bench

tls-proxy-bench: tls_proxy_bench.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Wformat=2 \
		-pthread $(LDFLAGS) -o $@ $< $(LDLIBS)

check: tls-proxy-bench
	python3 ../tests/test_proxy.py ./tls-proxy-bench

clean:
	$(RM) tls-proxy-bench
