# SPDX-License-Identifier: GPL-2.0-only
CC ?= cc
CFLAGS ?= -O2

.PHONY: all clean
all: memcpy-bench

memcpy-bench: memcpy_bench.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Wformat=2 \
		$(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) memcpy-bench
