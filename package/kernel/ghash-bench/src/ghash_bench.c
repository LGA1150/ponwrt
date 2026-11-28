// SPDX-License-Identifier: GPL-2.0-only
/* GHASH throughput through the same ahash frontend used by generic GCM. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/aes.h>
#include <crypto/hash.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kmod.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/sched/cputime.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#define BENCH_BLOCK 16
#define BENCH_MAX_SIZE 65536
#define BENCH_MAX_DEPTH 128
#define BENCH_MAX_SG 256
#define BENCH_POISON 0xa5

static char *drivers[8] = {
	"ghash", "ghash-generic",
#ifdef CONFIG_ARM64
	"ghash-neon",
#endif
};
static unsigned int num_drivers = IS_ENABLED(CONFIG_ARM64) ? 3 : 2;
module_param_array(drivers, charp, &num_drivers, 0444);
MODULE_PARM_DESC(drivers, "GHASH algorithm/driver names; ARM64 defaults include ghash-neon");
static unsigned int lengths[32] = {
	16, 64, 256, 1024, 1440, 4096, 16384, 16400, 65536,
};
static unsigned int num_lengths = 9;
module_param_array(lengths, uint, &num_lengths, 0444);
MODULE_PARM_DESC(lengths, "Exact payload lengths: 1..65536 bytes");
static unsigned int depths[16] = { 1, 32 };
static unsigned int num_depths = 2;
module_param_array(depths, uint, &num_depths, 0444);
MODULE_PARM_DESC(depths, "Maximum outstanding ahash requests: 1..128");
static int aad_len = -1;
module_param(aad_len, int, 0444);
MODULE_PARM_DESC(aad_len, "-1: raw GHASH; 0..4096: prepend GCM AAD, padding and length block");
static unsigned int sg_chunk;
module_param(sg_chunk, uint, 0444);
MODULE_PARM_DESC(sg_chunk, "Bytes per SG entry; 0: single SG, at most 256 entries");
static unsigned int duration_ms = 250;
module_param(duration_ms, uint, 0444);
MODULE_PARM_DESC(duration_ms, "Submission window per trial: 1..60000 ms, final drain also timed");
static unsigned int warmup_ms = 50;
module_param(warmup_ms, uint, 0444);
MODULE_PARM_DESC(warmup_ms, "Untimed warmup per driver/length/depth: 0..60000 ms");
static unsigned int runs = 3;
module_param(runs, uint, 0444);
MODULE_PARM_DESC(runs, "Timed trials per driver/length/depth: 1..20");
static unsigned int seed = 1708171853;
module_param(seed, uint, 0444);
MODULE_PARM_DESC(seed, "Seed for reproducible software-AES payloads and GHASH key");
static unsigned int timeout_ms = 10000;
module_param(timeout_ms, uint, 0444);
MODULE_PARM_DESC(timeout_ms, "Warn on a stall, then retain live requests until completion");
static unsigned int rows;
module_param(rows, uint, 0444);
static unsigned long long requests;
module_param(requests, ullong, 0444);

struct bench_buffer {
	unsigned long allocation;
	unsigned int order, size;
	u8 *data;
};

struct bench_slot {
	struct bench_buffer src, dst;
	struct scatterlist *sg;
	struct ahash_request *req;
	struct crypto_wait wait;
	u8 *input;
	u8 expected[BENCH_BLOCK];
	bool pending, used;
};

struct bench_totals {
	u64 count, async, backlogs, elapsed, cpu;
};

static unsigned int alignment;

/* Deliberately independent of gf128mul and the selected Crypto API driver. */
static void bench_multiply(u8 *value, const u8 *key)
{
	u8 v[BENCH_BLOCK], z[BENCH_BLOCK] = {};
	unsigned int bit, j;
	u8 carry;

	memcpy(v, key, sizeof(v));
	for (bit = 0; bit < 128; bit++) {
		if (value[bit / 8] & (0x80 >> (bit & 7)))
			for (j = 0; j < BENCH_BLOCK; j++)
				z[j] ^= v[j];
		carry = v[BENCH_BLOCK - 1] & 1;
		for (j = BENCH_BLOCK - 1; j; j--)
			v[j] = (v[j] >> 1) | (v[j - 1] << 7);
		v[0] >>= 1;
		if (carry)
			v[0] ^= 0xe1;
	}
	memcpy(value, z, sizeof(z));
}

static void bench_reference(const u8 *key, const u8 *input, unsigned int len,
			    u8 *digest)
{
	unsigned int pos, j;

	memset(digest, 0, BENCH_BLOCK);
	for (pos = 0; pos < len; pos += BENCH_BLOCK) {
		for (j = 0; j < min_t(unsigned int, BENCH_BLOCK, len - pos); j++)
			digest[j] ^= input[pos + j];
		bench_multiply(digest, key);
		if (!(pos & 2047))
			cond_resched();
	}
}

static int bench_reference_kat(void)
{
	/* Partial-block vector from crypto/testmgr.h: ghash_tv_template. */
	static const u8 input[] = "what do ya want for nothing?";
	static const u8 expected[BENCH_BLOCK] = {
		0x3e, 0x1f, 0x5c, 0x4d, 0x65, 0xf0, 0xef, 0xce,
		0x0d, 0x61, 0x06, 0x27, 0x66, 0x51, 0xd5, 0xe2,
	};
	u8 key[BENCH_BLOCK], result[BENCH_BLOCK];

	memset(key, 0x0b, sizeof(key));
	bench_reference(key, input, sizeof(input) - 1, result);
	return memcmp(result, expected, sizeof(result)) ? -EBADMSG : 0;
}

static unsigned int bench_input_length(unsigned int len)
{
	if (aad_len < 0)
		return len;
	return ALIGN(aad_len, BENCH_BLOCK) + ALIGN(len, BENCH_BLOCK) + BENCH_BLOCK;
}

static unsigned int bench_nents(unsigned int len)
{
	return sg_chunk ? DIV_ROUND_UP(len, sg_chunk) : 1;
}

static int bench_buffer_alloc(struct bench_buffer *b, unsigned int len)
{
	b->size = ALIGN(len, alignment) + 2 * alignment;
	b->order = get_order(b->size);
	b->allocation = __get_free_pages(GFP_KERNEL, b->order);
	if (!b->allocation)
		return -ENOMEM;
	b->data = (u8 *)b->allocation + alignment;
	memset((void *)b->allocation, BENCH_POISON, b->size);
	return 0;
}

static int bench_buffer_check(struct bench_buffer *b, const u8 *expected,
			      unsigned int len)
{
	if (memchr_inv((void *)b->allocation, BENCH_POISON, alignment) ||
	    memchr_inv(b->data + len, BENCH_POISON, b->size - alignment - len))
		return -EOVERFLOW;
	return memcmp(b->data, expected, len) ? -EBADMSG : 0;
}

static void bench_fill(const struct crypto_aes_ctx *aes, u8 *dst,
		       unsigned int len, unsigned int payload_len, unsigned int index)
{
	u8 counter[AES_BLOCK_SIZE], block[AES_BLOCK_SIZE];
	unsigned int pos;

	put_unaligned_be32(seed, counter);
	put_unaligned_be32(payload_len, counter + 4);
	put_unaligned_be32(index, counter + 8);
	for (pos = 0; pos < len; pos += AES_BLOCK_SIZE) {
		put_unaligned_be32(pos / AES_BLOCK_SIZE + 1, counter + 12);
		aes_encrypt(aes, block, counter);
		memcpy(dst + pos, block, min_t(unsigned int, AES_BLOCK_SIZE, len - pos));
	}
}

static int bench_slot_init(struct bench_slot *s, struct crypto_ahash *tfm,
			   const struct crypto_aes_ctx *aes, const u8 *key,
			   unsigned int len, unsigned int index)
{
	unsigned int size = bench_input_length(len), nents = bench_nents(size);
	unsigned int i, pos = 0, chunk;
	int err;

	err = bench_buffer_alloc(&s->src, size);
	if (err)
		return err;
	err = bench_buffer_alloc(&s->dst, BENCH_BLOCK);
	if (err)
		return err;
	s->sg = kcalloc(nents, sizeof(*s->sg), GFP_KERNEL);
	s->req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!s->sg || !s->req)
		return -ENOMEM;
	crypto_init_wait(&s->wait);
	bench_fill(aes, s->src.data, size, len, index);
	if (aad_len >= 0) {
		unsigned int padded_aad = ALIGN(aad_len, BENCH_BLOCK);

		memset(s->src.data + aad_len, 0, padded_aad - aad_len);
		memset(s->src.data + padded_aad + len, 0, ALIGN(len, BENCH_BLOCK) - len);
		put_unaligned_be64((u64)aad_len * 8, s->src.data + size - BENCH_BLOCK);
		put_unaligned_be64((u64)len * 8, s->src.data + size - 8);
	}
	s->input = kmemdup(s->src.data, size, GFP_KERNEL);
	if (!s->input)
		return -ENOMEM;
	bench_reference(key, s->input, size, s->expected);
	sg_init_table(s->sg, nents);
	for (i = 0; i < nents; i++) {
		chunk = min(sg_chunk ?: size, size - pos);
		sg_set_buf(&s->sg[i], s->src.data + pos, chunk);
		pos += chunk;
	}
	ahash_request_set_callback(s->req, CRYPTO_TFM_REQ_MAY_SLEEP |
				   CRYPTO_TFM_REQ_MAY_BACKLOG, crypto_req_done, &s->wait);
	ahash_request_set_crypt(s->req, s->sg, s->dst.data, size);
	return 0;
}

static int bench_wait(struct bench_slot *s)
{
	bool expired = false;

	if (!s->pending)
		return 0;
	if (!wait_for_completion_timeout(&s->wait.completion,
					 msecs_to_jiffies(timeout_ms))) {
		pr_err("request stalled; retaining live buffers until completion\n");
		expired = true;
		/* The Crypto API cannot cancel a request already owned by a driver. */
		wait_for_completion(&s->wait.completion);
	}
	s->pending = false;
	return expired ? -ETIMEDOUT : s->wait.err;
}

static int bench_drain(struct bench_slot *slots, unsigned int depth)
{
	unsigned int i;
	int err = 0, ret;

	for (i = 0; i < depth; i++) {
		ret = bench_wait(&slots[i]);
		if (!err)
			err = ret;
	}
	return err;
}

static void bench_slots_free(struct bench_slot *slots, unsigned int depth)
{
	unsigned int i;

	for (i = 0; i < depth; i++) {
		struct bench_slot *s = &slots[i];

		ahash_request_free(s->req);
		kfree(s->sg);
		kfree_sensitive(s->input);
		if (s->src.allocation)
			free_pages(s->src.allocation, s->src.order);
		if (s->dst.allocation)
			free_pages(s->dst.allocation, s->dst.order);
	}
	kfree_sensitive(slots);
}

static int bench_submit(struct bench_slot *s, struct bench_totals *t)
{
	int err;

	reinit_completion(&s->wait.completion);
	s->wait.err = 0;
	err = crypto_ahash_digest(s->req);
	if (err == -EINPROGRESS || err == -EBUSY) {
		/* MAY_BACKLOG makes -EBUSY an accepted, queued request. */
		s->pending = true;
		t->async++;
		t->backlogs += err == -EBUSY;
	} else if (err) {
		return err;
	}
	s->used = true;
	t->count++;
	return 0;
}

static int bench_check(struct bench_slot *slots, unsigned int depth, unsigned int len)
{
	unsigned int i;
	int err;

	for (i = 0; i < depth; i++) {
		struct bench_slot *s = &slots[i];

		err = bench_buffer_check(&s->src, s->input, bench_input_length(len));
		if (!err && s->used)
			err = bench_buffer_check(&s->dst, s->expected, BENCH_BLOCK);
		if (err) {
			pr_err("verification failed: slot=%u len=%u err=%d\n", i, len, err);
			if (s->used)
				pr_err("digest: expected=%*phN actual=%*phN\n",
				       BENCH_BLOCK, s->expected, BENCH_BLOCK, s->dst.data);
			return err;
		}
	}
	return 0;
}

static int bench_preflight(struct bench_slot *slots, unsigned int depth)
{
	struct bench_totals t = {};
	unsigned int i;
	int err = 0, ret;

	for (i = 0; i < depth; i++) {
		err = bench_submit(&slots[i], &t);
		if (err)
			break;
	}
	ret = bench_drain(slots, depth);
	return err ?: ret;
}

static int bench_window(struct bench_slot *slots, unsigned int depth,
			unsigned int duration, struct bench_totals *t)
{
	u64 start, end, user_start, sys_start, user_end, sys_end;
	unsigned int i;
	int err = 0, ret;

	memset(t, 0, sizeof(*t));
	for (i = 0; i < depth; i++) {
		slots[i].used = false;
		memset(slots[i].dst.data, BENCH_POISON, BENCH_BLOCK);
	}
	i = 0;
	task_cputime_adjusted(current, &user_start, &sys_start);
	start = ktime_get_ns();
	end = start + (u64)duration * NSEC_PER_MSEC;
	do {
		err = bench_wait(&slots[i]);
		if (err)
			break;
		if (fatal_signal_pending(current)) {
			err = -EINTR;
			break;
		}
		if (t->count && ktime_get_ns() >= end)
			break;
		err = bench_submit(&slots[i], t);
		if (err)
			break;
		i = (i + 1) % depth;
		if (!(t->count & 255))
			cond_resched();
	} while (ktime_get_ns() < end);
	ret = bench_drain(slots, depth);
	t->elapsed = ktime_get_ns() - start;
	task_cputime_adjusted(current, &user_end, &sys_end);
	t->cpu = user_end - user_start + sys_end - sys_start;
	return err ?: ret;
}

static int bench_case(struct crypto_ahash *tfm, const char *requested,
		      const struct crypto_aes_ctx *aes, const u8 *key,
		      unsigned int len, unsigned int depth)
{
	unsigned int size = bench_input_length(len), i, trial = 0;
	struct bench_totals t;
	struct bench_slot *slots;
	int err;

	slots = kcalloc(depth, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return -ENOMEM;
	for (i = 0; i < depth; i++) {
		if (fatal_signal_pending(current)) {
			err = -EINTR;
			goto out;
		}
		err = bench_slot_init(&slots[i], tfm, aes, key, len, i);
		if (err)
			goto out;
	}
	err = bench_preflight(slots, depth);
	if (!err)
		err = bench_check(slots, depth, len);
	if (err)
		goto out;
	if (warmup_ms) {
		err = bench_window(slots, depth, warmup_ms, &t);
		if (!err)
			err = bench_check(slots, depth, len);
		if (err)
			goto out;
	}
	for (trial = 1; trial <= runs; trial++) {
		u64 bytes, hashed, rate, hash_rate;

		err = bench_window(slots, depth, duration_ms, &t);
		if (!err)
			err = bench_check(slots, depth, len);
		if (err)
			goto out;
		bytes = t.count * len;
		hashed = t.count * ALIGN(size, BENCH_BLOCK);
		rate = mul_u64_u64_div_u64(bytes, 8 * NSEC_PER_MSEC, t.elapsed);
		hash_rate = mul_u64_u64_div_u64(hashed, 8 * NSEC_PER_MSEC, t.elapsed);
		pr_info("row,%s,%s,%s,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,"
			"%llu.%03llu,%llu.%03llu,%llu,%llu,%llu,%llu\n",
			aad_len < 0 ? "raw" : "gcm", requested, crypto_ahash_driver_name(tfm),
			len, size, depth, bench_nents(size), trial, t.count, bytes, hashed,
			t.elapsed, div_u64(rate, 1000), rate % 1000,
			div_u64(hash_rate, 1000), hash_rate % 1000,
			div64_u64(t.elapsed, t.count), t.cpu, t.async, t.backlogs);
		rows++;
		requests += t.count;
	}
out:
	if (err)
		pr_err("case failed: driver=%s len=%u depth=%u run=%u err=%d\n",
		       requested, len, depth, trial, err);
	bench_slots_free(slots, depth);
	return err;
}

static int __init ghash_bench_init(void)
{
	struct crypto_ahash *tfms[ARRAY_SIZE(drivers)] = {};
	u8 aes_key[AES_KEYSIZE_128], key[BENCH_BLOCK], zero[BENCH_BLOCK] = {};
	struct crypto_aes_ctx aes;
	unsigned int i, j, k;
	int err;

	if (!num_drivers || !num_lengths || !num_depths ||
	    aad_len < -1 || aad_len > 4096 || sg_chunk > BENCH_MAX_SIZE ||
	    !duration_ms || duration_ms > 60000 || warmup_ms > 60000 ||
	    !runs || runs > 20 || !timeout_ms || timeout_ms > 60000)
		return -EINVAL;
	for (i = 0; i < num_lengths; i++)
		if (!lengths[i] || lengths[i] > BENCH_MAX_SIZE ||
		    bench_nents(bench_input_length(lengths[i])) > BENCH_MAX_SG)
			return -EINVAL;
	for (i = 0; i < num_depths; i++)
		if (!depths[i] || depths[i] > BENCH_MAX_DEPTH)
			return -EINVAL;
	for (i = 0; i < num_drivers; i++) {
		if (!drivers[i] || !*drivers[i])
			return -EINVAL;
		/* Built-in GHASH needs no module; the stock module lacks this alias. */
		if (IS_ENABLED(CONFIG_ARM64) && !strcmp(drivers[i], "ghash-neon") &&
		    !crypto_has_shash("ghash-neon", 0, 0)) {
			err = request_module("ghash-ce");
			if (err) {
				pr_err("cannot load ghash-ce for ghash-neon: %d\n", err);
				return err;
			}
		}
	}
	alignment = max_t(unsigned int, 128, dma_get_cache_alignment());
	if (!is_power_of_2(alignment) || alignment > PAGE_SIZE)
		return -EINVAL;
	err = bench_reference_kat();
	if (err) {
		pr_err("independent GHASH reference failed its testmgr vector\n");
		return err;
	}
	for (i = 0; i < sizeof(aes_key); i++)
		aes_key[i] = i ^ (seed >> ((i & 3) * 8));
	err = aes_expandkey(&aes, aes_key, sizeof(aes_key));
	memzero_explicit(aes_key, sizeof(aes_key));
	if (err)
		goto out;
	/* H = AES_K(0), as in GCM; the same H applies to every comparison. */
	aes_encrypt(&aes, key, zero);
	for (i = 0; i < num_drivers; i++) {
		struct crypto_ahash *tfm = crypto_alloc_ahash(drivers[i], 0, 0);

		if (IS_ERR(tfm)) {
			err = PTR_ERR(tfm);
			pr_err("cannot allocate requested GHASH driver %s: %d\n", drivers[i], err);
			goto out;
		}
		tfms[i] = tfm;
		if (strcmp(crypto_ahash_alg_name(tfm), "ghash") ||
		    crypto_ahash_digestsize(tfm) != BENCH_BLOCK ||
		    crypto_ahash_blocksize(tfm) != BENCH_BLOCK) {
			err = -EINVAL;
			goto out;
		}
		err = crypto_ahash_setkey(tfm, key, sizeof(key));
		if (err)
			goto out;
		pr_info("requested=%s target=%s\n", drivers[i], crypto_ahash_driver_name(tfm));
	}
	pr_info("seed=%u alignment=%u aad_len=%d sg_chunk=%u duration_ms=%u warmup_ms=%u runs=%u\n",
		seed, alignment, aad_len, sg_chunk, duration_ms, warmup_ms, runs);
	pr_info("api=ahash_digest submitters=1 payload=software-aes reference=bit-serial-gf128\n");
	pr_info("verify=preflight+final-slots+guards; intermediate digests are reused\n");
	pr_info("kind,format,requested,driver,length,input_length,depth,sg_nents,run,"
		"requests,payload_bytes,hashed_bytes,elapsed_ns,mbps,hash_mbps,"
		"ns_per_request,submit_cpu_ns,async,backlogs\n");
	for (i = 0; i < num_drivers; i++)
		for (j = 0; j < num_lengths; j++)
			for (k = 0; k < num_depths; k++) {
				err = bench_case(tfms[i], drivers[i], &aes, key, lengths[j], depths[k]);
				if (err)
					goto out;
			}
out:
	for (i = 0; i < num_drivers; i++)
		if (tfms[i])
			crypto_free_ahash(tfms[i]);
	memzero_explicit(&aes, sizeof(aes));
	memzero_explicit(key, sizeof(key));
	pr_info("%s err=%d rows=%u timed_requests=%llu\n", err ? "FAIL" : "PASS",
		err, rows, requests);
	return err;
}

static void __exit ghash_bench_exit(void)
{
}

module_init(ghash_bench_init);
module_exit(ghash_bench_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH implementation, length and queue-depth throughput sweeps");
