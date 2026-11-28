// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "ghash_user.h"
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifndef __aarch64__
#error "This benchmark requires an AArch64 userspace toolchain"
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_SIZES 64
#define MAX_ALGS 128
#define MAX_LENGTH (16U * 1024 * 1024)
#define GUARD 128
#define ALIGNMENT 128
#define HWCAP_ASIMD_BIT (1UL << 1)

struct digest {
	uint64_t before[2], state[2], after[2];
};

struct buffer {
	uint8_t *base, *data, *original;
	size_t size, len, padded;
};

struct timing {
	uint64_t requests, elapsed, cpu;
};

static const char *algorithms = "ghash-neon-schoolbook,ghash-neon-4x,"
	"ghash-neon-4x-pmull13,ghash-neon-4x-schoolbook";
static const char *size_list = "16,32,64,1440,16384,16400,65536";
static unsigned int duration_ms = 1000, warmup_ms = 100, runs = 5, batch = 64;
static uint32_t seed = 1708171853;
static unsigned int offset;
static int cpu = -1;
static bool selftest_only;

static void die(const char *message)
{
	fprintf(stderr, "ghash-bench: %s\n", message);
	exit(EXIT_FAILURE);
}

static void syserr(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n);

	if (!p)
		syserr("malloc");
	return p;
}

static uint64_t ns(clockid_t id)
{
	struct timespec ts;

	if (clock_gettime(id, &ts))
		syserr("clock_gettime");
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static uint32_t rotl(uint32_t x, unsigned int n)
{
	return (x << n) | (x >> (32 - n));
}

static void quarter(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
	*a += *b; *d = rotl(*d ^ *a, 16);
	*c += *d; *b = rotl(*b ^ *c, 12);
	*a += *b; *d = rotl(*d ^ *a, 8);
	*c += *d; *b = rotl(*b ^ *c, 7);
}

static void chacha_block(const uint32_t input[16], uint8_t output[64])
{
	uint32_t x[16];

	memcpy(x, input, sizeof(x));
	for (unsigned int i = 0; i < 10; i++) {
		quarter(&x[0], &x[4], &x[8], &x[12]);
		quarter(&x[1], &x[5], &x[9], &x[13]);
		quarter(&x[2], &x[6], &x[10], &x[14]);
		quarter(&x[3], &x[7], &x[11], &x[15]);
		quarter(&x[0], &x[5], &x[10], &x[15]);
		quarter(&x[1], &x[6], &x[11], &x[12]);
		quarter(&x[2], &x[7], &x[8], &x[13]);
		quarter(&x[3], &x[4], &x[9], &x[14]);
	}
	for (unsigned int i = 0; i < 16; i++) {
		x[i] += input[i];
		for (unsigned int j = 0; j < 4; j++)
			output[i * 4 + j] = x[i] >> (8 * j);
	}
}

static void payload(uint8_t *out, size_t len, uint32_t stream_seed)
{
	uint32_t state[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
	uint8_t block[64];

	for (unsigned int i = 0; i < 8; i++)
		state[4 + i] = (0x03020100U + i * 0x04040404U) ^ stream_seed;
	state[12] = 1;
	state[13] = stream_seed;
	state[14] = ~stream_seed;
	while (len) {
		size_t take = len < 64 ? len : 64;

		chacha_block(state, block);
		memcpy(out, block, take);
		out += take;
		len -= take;
		state[12]++;
	}
}

static void digest_init(struct digest *d)
{
	memset(d, 0xa5, sizeof(*d));
	d->state[0] = d->state[1] = 0;
}

static void digest_check(const struct digest *d, const uint8_t expected[16],
			 const char *alg, size_t len)
{
	uint8_t actual[16];
	const uint64_t guard = UINT64_C(0xa5a5a5a5a5a5a5a5);

	ghash_export(d->state, actual);
	if (memcmp(actual, expected, 16) || d->before[0] != guard ||
	    d->before[1] != guard || d->after[0] != guard || d->after[1] != guard) {
		fprintf(stderr, "ghash-bench: digest/guard mismatch alg=%s len=%zu\n",
			alg, len);
		exit(EXIT_FAILURE);
	}
}

static void buffer_init(struct buffer *b, size_t len, unsigned int off,
			unsigned int pattern)
{
	int err;

	b->len = len;
	b->padded = (len + 15) & ~(size_t)15;
	b->size = GUARD + off + b->padded + GUARD;
	err = posix_memalign((void **)&b->base, ALIGNMENT, b->size);
	if (err) {
		errno = err;
		syserr("posix_memalign");
	}
	memset(b->base, 0xa5, b->size);
	b->data = b->base + GUARD + off;
	if (pattern < 2)
		memset(b->data, pattern ? 0xff : 0, len);
	else
		payload(b->data, len, seed ^ pattern);
	memset(b->data + len, 0, b->padded - len);
	b->original = xmalloc(b->size);
	memcpy(b->original, b->base, b->size);
}

static void buffer_check(const struct buffer *b)
{
	if (memcmp(b->base, b->original, b->size))
		die("input, padding or input guards were modified");
}

static void buffer_free(struct buffer *b)
{
	free(b->base);
	free(b->original);
}

static void known_answers(const size_t *selected, size_t count)
{
	static const uint8_t h[16] = {
		0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e
	};
	static const uint8_t in[32] = {
		0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80
	};
	static const uint8_t expected[2][16] = {
		{0x5e,0x2e,0xc7,0x46,0x91,0x70,0x62,0x88,0x2c,0x85,0xb0,0x68,0x53,0x53,0xde,0xb7},
		{0xf3,0x8c,0xbb,0x1a,0xd6,0x92,0x23,0xdc,0xc3,0x45,0x7a,0xe5,0xb6,0xb0,0xf8,0x85}
	};
	static const uint8_t chacha_expected[64] = {
		0x10,0xf1,0xe7,0xe4,0xd1,0x3b,0x59,0x15,0x50,0x0f,0xdd,0x1f,0xa3,0x20,0x71,0xc4,
		0xc7,0xd1,0xf4,0xc7,0x33,0xc0,0x68,0x03,0x04,0x22,0xaa,0x9a,0xc3,0xd4,0x6c,0x4e,
		0xd2,0x82,0x64,0x46,0x07,0x9f,0xaa,0x09,0x14,0xc2,0xd7,0x05,0xd9,0x8b,0x02,0xa2,
		0xb5,0x12,0x9c,0xd1,0xde,0x16,0x4e,0xb9,0xcb,0xd0,0x83,0xe8,0xa2,0x50,0x3c,0x4e
	};
	uint32_t state[16] = {0x61707865,0x3320646e,0x79622d32,0x6b206574};
	uint8_t actual[64];

	for (unsigned int i = 0; i < 8; i++)
		state[4 + i] = 0x03020100U + 0x04040404U * i;
	state[12] = 1; state[13] = 0x09000000; state[14] = 0x4a000000;
	chacha_block(state, actual);
	if (memcmp(actual, chacha_expected, 64))
		die("RFC 8439 ChaCha20 known-answer failure");
	for (unsigned int n = 1; n <= 2; n++) {
		ghash_reference(h, in, 16 * n, actual);
		if (memcmp(actual, expected[n - 1], 16))
			die("portable GHASH known-answer failure");
		for (size_t i = 0; i < count; i++) {
			const struct variant *v = &variants[selected[i]];
			union ghash_key key;
			struct digest d;

			ghash_key_init(&key, v->kind, h);
			digest_init(&d);
			if (ghash_user_abi_check(v->blocks, n, d.state, in, &key))
				die("AAPCS64 register preservation failure");
			digest_check(&d, expected[n - 1], v->name, n * 16);
		}
	}
	printf("# PASS known_answers=GHASH+ChaCha20 ABI=preserved variants=%zu\n", count);
}

static void selftest(const size_t *selected, size_t count)
{
	static const size_t large[] = {
		383,384,385,511,512,513,1023,1024,1025,4095,4096,4097,
		16383,16384,16385,65535,65536,65537,131071,131072,131073,131201
	};
	static const unsigned int offsets[] = {0,1,7,15};
	union ghash_key *keys = xmalloc(count * sizeof(*keys));
	long ps = sysconf(_SC_PAGESIZE);
	size_t page, span, checks = 0;
	uint8_t *mapping;

	if (ps <= 0)
		die("invalid page size");
	page = (size_t)ps;
	span = ((131216 + page - 1) / page) * page;
	mapping = mmap(NULL, span + 2 * page, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || mprotect(mapping, page, PROT_NONE) ||
	    mprotect(mapping + page + span, page, PROT_NONE))
		syserr("guarded mmap");
	for (unsigned int pattern = 0; pattern < 4; pattern++) {
		uint8_t h[16];

		if (pattern < 2)
			memset(h, pattern ? 0xff : 0, 16);
		else
			payload(h, 16, seed ^ (pattern + 0x100));
		for (size_t a = 0; a < count; a++)
			ghash_key_init(&keys[a], variants[selected[a]].kind, h);
		for (size_t n = 0; n < 258 + ARRAY_SIZE(large); n++) {
			size_t len = n < 258 ? n : large[n - 258];
			uint8_t expected[16];

			for (size_t o = 0; o < ARRAY_SIZE(offsets); o++) {
				struct buffer b;

				buffer_init(&b, len, offsets[o], pattern);
				if (!o)
					ghash_reference(h, b.data, len, expected);
				for (size_t a = 0; a < count; a++) {
					const struct variant *v = &variants[selected[a]];
					unsigned int blocks = b.padded / 16;
					unsigned int first = blocks ? 1 : 0;
					unsigned int second = blocks - first < 3 ? blocks - first : 3;
					struct digest d;

					digest_init(&d);
					ghash_user_blocks(v->blocks, blocks, d.state, b.data, &keys[a]);
					digest_check(&d, expected, v->name, len);
					buffer_check(&b);
					checks++;
					digest_init(&d);
					ghash_user_blocks(v->blocks, first, d.state, b.data, &keys[a]);
					ghash_user_blocks(v->blocks, second, d.state, b.data + first * 16, &keys[a]);
					ghash_user_blocks(v->blocks, blocks - first - second, d.state,
							  b.data + (first + second) * 16, &keys[a]);
					digest_check(&d, expected, v->name, len);
					buffer_check(&b);
					checks++;
				}
				if (!o) {
					uint8_t *end = mapping + page + span - b.padded;

					memcpy(end, b.data, b.padded);
					for (size_t a = 0; a < count; a++) {
						const struct variant *v = &variants[selected[a]];
						struct digest d;

						digest_init(&d);
						ghash_user_blocks(v->blocks, b.padded / 16, d.state, end, &keys[a]);
						digest_check(&d, expected, v->name, len);
						if (memcmp(end, b.data, b.padded))
							die("guard-page input modified");
						checks++;
					}
				}
				buffer_free(&b);
			}
		}
		printf("# selftest pattern=%u passed checks=%zu\n", pattern, checks);
		fflush(stdout);
	}
	munmap(mapping, span + 2 * page);
	free(keys);
	printf("# PASS selftest variants=%zu checks=%zu max_length=131201 offsets=0,1,7,15 guard_page=enabled\n",
	       count, checks);
}

static struct timing window(const struct variant *v, const union ghash_key *key,
			    const struct buffer *b, struct digest *d,
			    unsigned int ms, unsigned int repeats)
{
	struct timing t = {0};
	uint64_t start_cpu = ns(CLOCK_THREAD_CPUTIME_ID);
	uint64_t start = ns(CLOCK_MONOTONIC_RAW), now;

	do {
		ghash_user_repeat(v->blocks, b->padded / 16, d->state, b->data, key, repeats);
		t.requests += repeats;
		now = ns(CLOCK_MONOTONIC_RAW);
	} while (now - start < (uint64_t)ms * 1000000);
	t.elapsed = now - start;
	t.cpu = ns(CLOCK_THREAD_CPUTIME_ID) - start_cpu;
	return t;
}

static int compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static void benchmark(const struct variant *v, size_t length)
{
	union ghash_key key;
	struct buffer b;
	struct digest d;
	uint8_t h[16], expected[16];
	double *rates = xmalloc(runs * sizeof(*rates));
	uint64_t total_bytes = 0, total_ns = 0;
	unsigned int repeats = batch;

	buffer_init(&b, length, offset, 2);
	payload(h, 16, seed ^ 0x100);
	ghash_key_init(&key, v->kind, h);
	ghash_reference(h, b.data, b.len, expected);
	digest_init(&d);
	ghash_user_blocks(v->blocks, b.padded / 16, d.state, b.data, &key);
	digest_check(&d, expected, v->name, length);
	buffer_check(&b);
	/* Limit a batch to at most 1 MiB, or one digest for larger inputs. */
	if (repeats > 1024 * 1024 / b.padded)
		repeats = 1024 * 1024 / b.padded;
	if (!repeats)
		repeats = 1;
	if (warmup_ms) {
		window(v, &key, &b, &d, warmup_ms, repeats);
		digest_check(&d, expected, v->name, length);
		buffer_check(&b);
	}
	for (unsigned int r = 0; r < runs; r++) {
		struct timing t = window(v, &key, &b, &d, duration_ms, repeats);
		uint64_t bytes = t.requests * length;
		uint64_t hash_bytes = t.requests * b.padded;

		digest_check(&d, expected, v->name, length);
		buffer_check(&b);
		rates[r] = (double)bytes * 8 / t.elapsed;
		total_bytes += bytes;
		total_ns += t.elapsed;
		printf("%s,%zu,%zu,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64
		       ",%" PRIu64 ",%.6f,%.6f,%.3f,%.6f\n",
		       v->name, length, b.padded, offset, repeats, r + 1,
		       t.requests, bytes, hash_bytes, t.elapsed, rates[r],
		       (double)hash_bytes * 8 / t.elapsed,
		       (double)t.elapsed / t.requests, (double)t.cpu / 1e9);
		fflush(stdout);
	}
	qsort(rates, runs, sizeof(*rates), compare_double);
	printf("# %s length=%zu gbps min=%.6f median=%.6f max=%.6f aggregate=%.6f verified=preflight+final+guards\n",
	       v->name, length, rates[0], (rates[(runs - 1) / 2] + rates[runs / 2]) / 2,
	       rates[runs - 1], (double)total_bytes * 8 / total_ns);
	free(rates);
	buffer_free(&b);
}

static unsigned int number(const char *s, unsigned int max)
{
	char *end;
	unsigned long long value;

	if (!s[0] || s[0] == '-' || s[0] == '+')
		die("expected an unsigned integer");
	errno = 0;
	value = strtoull(s, &end, 0);
	if (errno || *end || value > max)
		die("invalid or out-of-range number");
	return (unsigned int)value;
}

static size_t select_algorithms(size_t selected[MAX_ALGS])
{
	char *list, *next, *name;
	size_t count = 0;

	if (nr_variants > MAX_ALGS)
		die("variant table exceeds selection capacity");
	if (!strcmp(algorithms, "all")) {
		for (size_t i = 0; i < nr_variants; i++)
			selected[count++] = i;
		return count;
	}
	list = strdup(algorithms);
	if (!list)
		syserr("strdup");
	next = list;
	while ((name = strsep(&next, ","))) {
		size_t i;

		for (i = 0; i < nr_variants; i++)
			if (!strcmp(name, variants[i].name))
				break;
		if (i == nr_variants)
			die("unknown variant; use --list");
		if (count == MAX_ALGS)
			die("too many variants");
		for (size_t j = 0; j < count; j++)
			if (selected[j] == i)
				die("duplicate variant");
		selected[count++] = i;
	}
	free(list);
	return count;
}

static void usage(void)
{
	puts("Usage: ghash-bench [options]\n"
	     "  -a, --algorithms LIST  Comma-separated variant names, or all\n"
	     "  -s, --sizes LIST       Input bytes, 1..16777216; partial blocks padded\n"
	     "  -d, --duration MS      Timed window per trial (default 1000)\n"
	     "  -w, --warmup MS        Warmup per variant/length (default 100)\n"
	     "  -r, --runs N           Trials per variant/length (default 5)\n"
	     "  -b, --batch N          Calls per timing batch (default 64; capped at 1 MiB)\n"
	     "  -c, --cpu N            Pin this process to CPU N (default unpinned)\n"
	     "  -o, --offset N         Input offset from 128-byte alignment, 0..127\n"
	     "      --seed N           Payload/key seed (default 1708171853)\n"
	     "      --selftest         Run boundary/streaming/guard tests, then exit\n"
	     "      --list             List all compiled variants\n"
	     "  -h, --help             Show this help\n"
	     "Default variants: schoolbook, 4x, 4x-pmull13, 4x-schoolbook.\n"
	     "No root, Crypto API, kernel headers or loaded crypto modules required.");
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{"algorithms", required_argument, NULL, 'a'},
		{"sizes", required_argument, NULL, 's'},
		{"duration", required_argument, NULL, 'd'},
		{"warmup", required_argument, NULL, 'w'},
		{"runs", required_argument, NULL, 'r'},
		{"batch", required_argument, NULL, 'b'},
		{"cpu", required_argument, NULL, 'c'},
		{"offset", required_argument, NULL, 'o'},
		{"seed", required_argument, NULL, 256},
		{"selftest", no_argument, NULL, 257},
		{"list", no_argument, NULL, 258},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	size_t selected[MAX_ALGS], sizes[MAX_SIZES], count, nr_sizes = 0;
	struct utsname uts;
	char *list, *next, *item;
	int option;

	while ((option = getopt_long(argc, argv, "a:s:d:w:r:b:c:o:h", opts, NULL)) != -1) {
		switch (option) {
		case 'a': algorithms = optarg; break;
		case 's': size_list = optarg; break;
		case 'd': duration_ms = number(optarg, 60000); break;
		case 'w': warmup_ms = number(optarg, 60000); break;
		case 'r': runs = number(optarg, 100); break;
		case 'b': batch = number(optarg, 65536); break;
		case 'c': cpu = !strcmp(optarg, "-1") ? -1 : (int)number(optarg, CPU_SETSIZE - 1); break;
		case 'o': offset = number(optarg, ALIGNMENT - 1); break;
		case 256: seed = number(optarg, UINT32_MAX); break;
		case 257: selftest_only = true; break;
		case 258:
			for (size_t i = 0; i < nr_variants; i++)
				puts(variants[i].name);
			return 0;
		case 'h': usage(); return 0;
		default: usage(); return 1;
		}
	}
	if (optind != argc || !duration_ms || !runs || !batch)
		die("unexpected argument or zero duration/runs/batch");
	count = select_algorithms(selected);
	list = strdup(size_list);
	if (!list)
		syserr("strdup");
	next = list;
	while ((item = strsep(&next, ","))) {
		unsigned int len = number(item, MAX_LENGTH);

		if (!len || nr_sizes == MAX_SIZES)
			die("invalid length or too many sizes");
		for (size_t i = 0; i < nr_sizes; i++)
			if (sizes[i] == len)
				die("duplicate size");
		sizes[nr_sizes++] = len;
	}
	free(list);
	if (!(getauxval(AT_HWCAP) & HWCAP_ASIMD_BIT))
		die("ASIMD is required; the optional 64-bit PMULL extension is not");
	if (cpu >= 0) {
		cpu_set_t set;

		CPU_ZERO(&set);
		CPU_SET(cpu, &set);
		if (sched_setaffinity(0, sizeof(set), &set))
			syserr("sched_setaffinity");
	}
	if (uname(&uts))
		syserr("uname");
	printf("# kernel=%s machine=%s api=userspace-blocks compiler=%s\n",
	       uts.release, uts.machine, __VERSION__);
	printf("# seed=%u alignment=128 offset=%u cpu=%d batch_limit=%u duration_ms=%u warmup_ms=%u runs=%u\n",
	       seed, offset, cpu, batch, duration_ms, warmup_ms, runs);
	puts("# payload=chacha20-c reference=bit-serial-gf128; preparation and verification untimed");
	puts("# Reuse one input buffer; reset digest per call; intermediate timed digests are reused.");
	known_answers(selected, count);
	if (selftest_only) {
		selftest(selected, count);
		return 0;
	}
	puts("algorithm,length,hashed_length,offset,batch,run,requests,payload_bytes,hashed_bytes,elapsed_ns,gbps,hash_gbps,ns_per_request,cpu_s");
	for (size_t a = 0; a < count; a++)
		for (size_t n = 0; n < nr_sizes; n++)
			benchmark(&variants[selected[a]], sizes[n]);
	printf("# PASS variants=%zu rows=%zu\n", count, count * nr_sizes * runs);
	return 0;
}
