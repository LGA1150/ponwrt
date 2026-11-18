// SPDX-License-Identifier: GPL-2.0-only
/* Streaming libc memcpy measurements; build: cc -O2 -o memcpy-bench memcpy_bench.c */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define MIB ((size_t)1024 * 1024)
#define MAX_BYTES (128 * MIB)
#define MAX_SIZES 32
#define MAX_RUNS 100
#define GUARD 0xa5

/* A volatile, indirect call prevents builtin replacement, copy removal and
 * coalescing even with optimization or LTO. Measure the installed libc.
 */
static void *(*volatile copy_fn)(void *, const void *, size_t) = memcpy;

struct options {
	size_t sizes[MAX_SIZES], working_set;
	unsigned int num_sizes, duration_ms, warmup_ms, runs;
	int cpu;
};

struct buffer {
	unsigned char *allocation, *data;
	size_t alignment, size;
};

struct result {
	uint64_t passes, copies, bytes, elapsed_ns;
	double cpu_s, gbps;
	long minor_faults, major_faults;
};

static void die(const char *what)
{
	fprintf(stderr, "memcpy-bench: %s: %s\n", what, strerror(errno));
	exit(EXIT_FAILURE);
}

static void invalid(const char *what)
{
	fprintf(stderr, "memcpy-bench: %s\n", what);
	exit(EXIT_FAILURE);
}

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: memcpy-bench [options]\n"
		"  -s, --sizes LIST       Copy sizes, comma-separated, 1 byte..128 MiB\n"
		"  -W, --working-set SIZE Bytes per source/destination buffer (default 128M)\n"
		"  -d, --duration MS      Minimum timed duration per trial (default 250)\n"
		"  -w, --warmup MS        Warmup per copy size (default 50; 0 disables)\n"
		"  -r, --runs N           Trials per copy size (default 3; maximum 100)\n"
		"  -c, --cpu N            Pin to CPU N (default -1: current affinity)\n"
		"  -h, --help             Show this help\n"
		"Sizes accept bytes or binary K/KiB/M/MiB suffixes.\n"
		"Default sizes: 16,64,256,1K,1440,4K,16K,64K,256K,1M,4M,16M,64M,128M.\n"
		"Each trial completes whole passes through both buffers. A final copy\n"
		"may be shorter than the selected block. Allocation and verification\n"
		"are untimed; default payload storage is 256 MiB plus small guards.\n");
}

static unsigned long long parse_decimal(const char *s, char **end)
{
	unsigned long long v;

	if (!s[0] || !isdigit((unsigned char)s[0]))
		invalid("expected an unsigned decimal number");
	errno = 0;
	v = strtoull(s, end, 10);
	if (errno)
		die("number out of range");
	return v;
}

static size_t parse_size(const char *s)
{
	unsigned long long v;
	size_t scale = 1;
	char *end;

	v = parse_decimal(s, &end);
	if (!strcasecmp(end, "K") || !strcasecmp(end, "KiB"))
		scale = 1024;
	else if (!strcasecmp(end, "M") || !strcasecmp(end, "MiB"))
		scale = MIB;
	else if (*end && strcasecmp(end, "B"))
		invalid("unknown size suffix; use bytes, K, KiB, M or MiB");
	if (!v || v > MAX_BYTES / scale)
		invalid("sizes must be between 1 byte and 128 MiB");
	return (size_t)v * scale;
}

static unsigned int parse_uint(const char *s, unsigned int low, unsigned int high)
{
	unsigned long long v;
	char *end;

	v = parse_decimal(s, &end);
	if (*end || v < low || v > high)
		invalid("numeric option outside its supported range");
	return (unsigned int)v;
}

static void parse_sizes(struct options *o, const char *arg)
{
	char *list = strdup(arg), *next, *s;

	if (!list)
		die("size list allocation");
	next = list;
	o->num_sizes = 0;
	while ((s = strsep(&next, ",")) != NULL) {
		if (o->num_sizes == MAX_SIZES)
			invalid("at most 32 copy sizes may be selected");
		o->sizes[o->num_sizes++] = parse_size(s);
	}
	free(list);
}

static void parse_options(struct options *o, int argc, char **argv)
{
	static const struct option options[] = {
		{ "sizes", required_argument, NULL, 's' },
		{ "working-set", required_argument, NULL, 'W' },
		{ "duration", required_argument, NULL, 'd' },
		{ "warmup", required_argument, NULL, 'w' },
		{ "runs", required_argument, NULL, 'r' },
		{ "cpu", required_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	unsigned int i;
	int c;

	while ((c = getopt_long(argc, argv, "s:W:d:w:r:c:h", options, NULL)) != -1) {
		switch (c) {
		case 's':
			parse_sizes(o, optarg);
			break;
		case 'W':
			o->working_set = parse_size(optarg);
			break;
		case 'd':
			o->duration_ms = parse_uint(optarg, 1, 60000);
			break;
		case 'w':
			o->warmup_ms = parse_uint(optarg, 0, 60000);
			break;
		case 'r':
			o->runs = parse_uint(optarg, 1, MAX_RUNS);
			break;
		case 'c':
			o->cpu = !strcmp(optarg, "-1") ? -1 :
				 (int)parse_uint(optarg, 0, CPU_SETSIZE - 1);
			break;
		case 'h':
			usage(stdout);
			exit(EXIT_SUCCESS);
		default:
			usage(stderr);
			exit(EXIT_FAILURE);
		}
	}
	if (optind != argc)
		invalid("unexpected positional argument");
	for (i = 0; i < o->num_sizes; i++)
		if (o->sizes[i] > o->working_set)
			invalid("working set must be at least the largest selected copy size");
}

static void pin_cpu(int cpu)
{
	cpu_set_t set;

	if (cpu < 0)
		return;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		die("sched_setaffinity");
}

static struct buffer allocate_buffer(size_t size, size_t alignment)
{
	struct buffer b = { .alignment = alignment, .size = size };
	void *p;
	int err;

	err = posix_memalign(&p, alignment, size + 2 * alignment);
	if (err) {
		errno = err;
		die("buffer allocation (two working-set buffers are needed)");
	}
	b.allocation = p;
	b.data = b.allocation + alignment;
	/* Touch every page before timing, including both guard regions. */
	memset(b.allocation, GUARD, size + 2 * alignment);
	return b;
}

static void fill_source(struct buffer *b)
{
	uint64_t state = UINT64_C(0x6a09e667f3bcc909);
	size_t i = 0, j;

	while (i < b->size) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		for (j = 0; j < sizeof(state) && i < b->size; j++, i++)
			b->data[i] = (unsigned char)(state >> (8 * j));
	}
}

static bool guard_ok(const struct buffer *b)
{
	size_t i;

	for (i = 0; i < b->alignment; i++)
		if (b->allocation[i] != GUARD || b->data[b->size + i] != GUARD)
			return false;
	return true;
}

static void verify(const struct buffer *src, const struct buffer *dst)
{
	size_t i;

	if (!guard_ok(src) || !guard_ok(dst))
		invalid("buffer guard corruption");
	if (!memcmp(src->data, dst->data, src->size))
		return;
	for (i = 0; i < src->size; i++) {
		if (src->data[i] == dst->data[i])
			continue;
		fprintf(stderr, "memcpy-bench: mismatch at byte %zu: expected=%02x actual=%02x\n",
			i, src->data[i], dst->data[i]);
		exit(EXIT_FAILURE);
	}
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts))
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static double cpu_seconds(const struct rusage *r)
{
	return (double)r->ru_utime.tv_sec + (double)r->ru_stime.tv_sec +
	       ((double)r->ru_utime.tv_usec + (double)r->ru_stime.tv_usec) / 1e6;
}

static struct result measure(const struct buffer *src, struct buffer *dst,
			     size_t block, unsigned int ms)
{
	struct result r = { 0 };
	struct rusage before, after;
	uint64_t start, elapsed, target = (uint64_t)ms * 1000000;
	size_t offset, whole = src->size / block * block;
	size_t tail = src->size - whole;

	/* Poison outside timing so a skipped whole trial cannot pass verification. */
	memset(dst->data, GUARD, dst->size);
	if (getrusage(RUSAGE_SELF, &before))
		die("getrusage");
	start = now_ns();
	do {
		for (offset = 0; offset < whole; offset += block)
			copy_fn(dst->data + offset, src->data + offset, block);
		if (tail)
			copy_fn(dst->data + whole, src->data + whole, tail);
		r.passes++;
		elapsed = now_ns() - start;
	} while (elapsed < target);
	if (getrusage(RUSAGE_SELF, &after))
		die("getrusage");
	if (!elapsed)
		invalid("clock did not advance");
	r.bytes = r.passes * src->size;
	r.copies = r.passes * (src->size / block + (tail != 0));
	r.elapsed_ns = elapsed;
	r.cpu_s = cpu_seconds(&after) - cpu_seconds(&before);
	r.minor_faults = after.ru_minflt - before.ru_minflt;
	r.major_faults = after.ru_majflt - before.ru_majflt;
	r.gbps = (double)r.bytes * 8 / (double)elapsed;
	verify(src, dst);
	return r;
}

static int compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static void benchmark(const struct options *o, const struct buffer *src,
		      struct buffer *dst, size_t block)
{
	double rates[MAX_RUNS], median, total_bytes = 0, total_ns = 0;
	unsigned int run;

	/* A single full pass checks the selected block size before timing trials. */
	measure(src, dst, block, 0);
	if (o->warmup_ms)
		measure(src, dst, block, o->warmup_ms);
	for (run = 0; run < o->runs; run++) {
		struct result r = measure(src, dst, block, o->duration_ms);
		double seconds = (double)r.elapsed_ns / 1e9;

		printf("%zu,%zu,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64
		       ",%" PRIu64 ",%.6f,%.6f,%.3f,%.6f,%.6f,%ld,%ld\n",
		       block, o->working_set, run + 1, r.passes, r.copies, r.bytes,
		       r.elapsed_ns, seconds, r.gbps, (double)r.bytes / MIB / seconds,
		       r.gbps * 2, r.cpu_s, r.minor_faults, r.major_faults);
		rates[run] = r.gbps;
		total_bytes += (double)r.bytes;
		total_ns += (double)r.elapsed_ns;
	}
	qsort(rates, o->runs, sizeof(rates[0]), compare_double);
	median = (rates[(o->runs - 1) / 2] + rates[o->runs / 2]) / 2;
	printf("# block_bytes=%zu gbps min=%.6f median=%.6f max=%.6f "
	       "aggregate=%.6f verified=data+guards\n",
	       block, rates[0], median, rates[o->runs - 1], total_bytes * 8 / total_ns);
}

int main(int argc, char **argv)
{
	struct options o = {
		.sizes = { 16, 64, 256, 1024, 1440, 4096, 16384, 65536,
			   256 * 1024, MIB, 4 * MIB, 16 * MIB, 64 * MIB, MAX_BYTES },
		.num_sizes = 14, .working_set = MAX_BYTES,
		.duration_ms = 250, .warmup_ms = 50, .runs = 3, .cpu = -1
	};
	struct buffer src, dst;
	struct utsname uts;
	unsigned int i;
	long page_size;

	parse_options(&o, argc, argv);
	pin_cpu(o.cpu);
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size < (long)sizeof(void *) || (page_size & (page_size - 1)))
		invalid("unsupported page alignment");
	if (uname(&uts))
		die("uname");
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("# kernel=%s machine=%s memcpy=libc threads=1 cpu=%d alignment=%ld\n",
	       uts.release, uts.machine, o.cpu, page_size);
	printf("# working_set_bytes=%zu total_buffer_bytes=%zu sizes=%u "
	       "duration_ms=%u warmup_ms=%u runs=%u\n",
	       o.working_set, 2 * (o.working_set + 2 * (size_t)page_size),
	       o.num_sizes, o.duration_ms, o.warmup_ms, o.runs);
	printf("# Streaming through separate buffers; "
	       "allocation, prefaulting and verification are untimed.\n");
	printf("# gbps counts copied payload; "
	       "nominal_rw_gbps is twice that, not measured DRAM traffic.\n");
	src = allocate_buffer(o.working_set, (size_t)page_size);
	dst = allocate_buffer(o.working_set, (size_t)page_size);
	fill_source(&src);
	printf("block_bytes,working_set_bytes,run,passes,copies,copied_bytes,"
	       "elapsed_ns,seconds,gbps,mib_per_s,nominal_rw_gbps,cpu_s,"
	       "minor_faults,major_faults\n");
	for (i = 0; i < o.num_sizes; i++)
		benchmark(&o, &src, &dst, o.sizes[i]);
	free(dst.allocation);
	free(src.allocation);
	return EXIT_SUCCESS;
}
