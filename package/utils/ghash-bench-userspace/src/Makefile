# SPDX-License-Identifier: GPL-2.0-only
CC ?= cc
CFLAGS ?= -O2
GHASH_SRC ?= ../../../kernel/crypto-ghash-neon-shuffle/src

WARNINGS := -Wall -Wextra -Wformat=2 -Wshadow
INCLUDES := -Icompat -I$(GHASH_SRC)
OBJECTS := ghash_bench.o ghash_keys.o ghash_call.o ghash-neon-core.o \
	ghash-neon-mul.o ghash-neon-schoolbook.o ghash-neon-intrinsics.o \
	ghash-neon-k32.o ghash-neon-k16.o ghash-neon-single-k32.o

.PHONY: all clean
all: ghash-bench

ghash-bench: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c ghash_user.h
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -std=c11 -MMD -MP -c -o $@ $<

ghash_call.o: ghash_call.S
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -MMD -MP -c -o $@ $<

ghash-neon-%.o: $(GHASH_SRC)/ghash-neon-%.S
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -MMD -MP -c -o $@ $<

ghash-neon-intrinsics.o: $(GHASH_SRC)/ghash-neon-intrinsics.c
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) $(WARNINGS) -std=c11 -MMD -MP -c -o $@ $<

-include $(OBJECTS:.o=.d)

clean:
	$(RM) ghash-bench $(OBJECTS) $(OBJECTS:.o=.d)
