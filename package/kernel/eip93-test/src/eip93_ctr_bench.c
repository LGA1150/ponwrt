// SPDX-License-Identifier: GPL-2.0-or-later
/* Aligned, out-of-place CTR throughput through the kernel Crypto API. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/aes.h>
#include <crypto/skcipher.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#define BENCH_POISON 0xa5
#define BENCH_MAX_SIZE 65536
#define BENCH_MAX_DEPTH 128

static char *driver = "ctr(aes-eip93)";
module_param(driver, charp, 0444);
MODULE_PARM_DESC(driver, "CTR driver name; default ctr(aes-eip93)");
static char *direction = "both";
module_param(direction, charp, 0444);
MODULE_PARM_DESC(direction, "encrypt, decrypt or both");
static unsigned int key_bits = 128;
module_param(key_bits, uint, 0444);
MODULE_PARM_DESC(key_bits, "AES key size: 128, 192 or 256 bits");
static unsigned int lengths[32] = { 16, 64, 256, 1024, 4096, 16384, 16400, 65536 };
static unsigned int num_lengths = 8;
module_param_array(lengths, uint, &num_lengths, 0444);
MODULE_PARM_DESC(lengths, "Exact payload lengths in bytes: 1..65536");
static unsigned int depths[16] = { 1, 32 };
static unsigned int num_depths = 2;
module_param_array(depths, uint, &num_depths, 0444);
MODULE_PARM_DESC(depths, "Maximum outstanding requests: 1..128");
static unsigned int duration_ms = 250;
module_param(duration_ms, uint, 0444);
MODULE_PARM_DESC(duration_ms, "Submission window per timed trial, 1..60000 ms; final drain is timed");
static unsigned int warmup_ms = 50;
module_param(warmup_ms, uint, 0444);
MODULE_PARM_DESC(warmup_ms, "Untimed warmup per length/depth/direction, 0..60000 ms");
static unsigned int runs = 3;
module_param(runs, uint, 0444);
MODULE_PARM_DESC(runs, "Timed trials per length/depth/direction, 1..20");
static unsigned int seed = 1708171853;
module_param(seed, uint, 0444);
MODULE_PARM_DESC(seed, "Reproducible software-CTR payload seed");
static unsigned int timeout_ms = 10000;
module_param(timeout_ms, uint, 0444);
MODULE_PARM_DESC(timeout_ms, "Warn on a stalled completion, then retain live buffers until it finishes");
static unsigned int rows;
module_param(rows, uint, 0444);
static unsigned long long requests;
module_param(requests, ullong, 0444);

struct bench_buffer {
	unsigned long allocation;
	u8 *data;
	unsigned int order, extent;
	struct scatterlist sg;
};

struct bench_slot {
	struct bench_buffer src, dst;
	struct skcipher_request *req;
	struct crypto_wait wait;
	u8 *plain, *cipher;
	u8 initial[AES_BLOCK_SIZE] __aligned(AES_BLOCK_SIZE);
	u8 iv[AES_BLOCK_SIZE] __aligned(AES_BLOCK_SIZE);
	u8 next[AES_BLOCK_SIZE];
	bool pending, used;
};

struct bench_totals {
	u64 count, async, backlog, elapsed;
};

static unsigned int alignment;

static int bench_buffer_alloc(struct bench_buffer *b, unsigned int len)
{
	b->extent = ALIGN(len, alignment);
	b->order = get_order(b->extent + 2 * alignment);
	b->allocation = __get_free_pages(GFP_KERNEL, b->order);
	if (!b->allocation)
		return -ENOMEM;
	b->data = (u8 *)b->allocation + alignment;
	memset((void *)b->allocation, BENCH_POISON, b->extent + 2 * alignment);
	/* Map whole cache lines while processing only the requested length. */
	sg_init_one(&b->sg, b->data, b->extent);
	return 0;
}

static int bench_buffer_check(struct bench_buffer *b, const u8 *expected,
			      unsigned int len)
{
	if (memchr_inv(b->data - alignment, BENCH_POISON, alignment) ||
	    memchr_inv(b->data + len, BENCH_POISON, b->extent - len + alignment))
		return -EOVERFLOW;
	return memcmp(b->data, expected, len) ? -EBADMSG : 0;
}

static int bench_reference(struct crypto_sync_skcipher *tfm, u8 *src, u8 *dst,
			   unsigned int len, u8 *iv)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	struct scatterlist in, out;
	int err;

	sg_init_one(&in, src, len);
	sg_init_one(&out, dst, len);
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);
	skcipher_request_set_crypt(req, &in, &out, len, iv);
	err = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
	return err;
}

static void bench_slots_free(struct bench_slot *slots, unsigned int depth)
{
	unsigned int i;

	if (!slots)
		return;
	for (i = 0; i < depth; i++) {
		struct bench_slot *s = &slots[i];

		skcipher_request_free(s->req);
		kfree_sensitive(s->plain);
		kfree_sensitive(s->cipher);
		if (s->src.allocation)
			free_pages(s->src.allocation, s->src.order);
		if (s->dst.allocation)
			free_pages(s->dst.allocation, s->dst.order);
	}
	kfree_sensitive(slots);
}

static int bench_slot_init(struct bench_slot *s, struct crypto_skcipher *tfm,
			   struct crypto_sync_skcipher *ref, unsigned int len,
			   unsigned int index, bool decrypt)
{
	u8 iv[AES_BLOCK_SIZE] __aligned(AES_BLOCK_SIZE);
	int err;

	err = bench_buffer_alloc(&s->src, len);
	if (err)
		return err;
	err = bench_buffer_alloc(&s->dst, len);
	if (err)
		return err;
	s->plain = kzalloc(len, GFP_KERNEL);
	s->cipher = kmalloc(len, GFP_KERNEL);
	s->req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!s->plain || !s->cipher || !s->req)
		return -ENOMEM;
	crypto_init_wait(&s->wait);
	put_unaligned_be32(seed, s->initial);
	put_unaligned_be32(len, s->initial + 4);
	put_unaligned_be32(index, s->initial + 8);
	put_unaligned_be32(1, s->initial + 12);
	memcpy(iv, s->initial, sizeof(iv));
	/* A separate counter domain generates the reproducible random payload. */
	iv[0] ^= 0x80;
	err = bench_reference(ref, s->plain, s->plain, len, iv);
	if (err)
		return err;
	memcpy(s->next, s->initial, sizeof(s->next));
	err = bench_reference(ref, s->plain, s->cipher, len, s->next);
	if (err)
		return err;
	memcpy(s->src.data, decrypt ? s->cipher : s->plain, len);
	skcipher_request_set_callback(s->req, CRYPTO_TFM_REQ_MAY_SLEEP |
				      CRYPTO_TFM_REQ_MAY_BACKLOG, crypto_req_done, &s->wait);
	skcipher_request_set_crypt(s->req, &s->src.sg, &s->dst.sg, len, s->iv);
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
		/* There is no Crypto API cancellation operation. */
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

static int bench_submit(struct bench_slot *s, bool decrypt, struct bench_totals *t)
{
	int err;

	memcpy(s->iv, s->initial, sizeof(s->iv));
	reinit_completion(&s->wait.completion);
	s->wait.err = 0;
	s->pending = true;
	err = decrypt ? crypto_skcipher_decrypt(s->req) : crypto_skcipher_encrypt(s->req);
	if (err == -EINPROGRESS || err == -EBUSY) {
		/* MAY_BACKLOG permits -EBUSY to accept a request for later processing. */
		t->async++;
		t->backlog += err == -EBUSY;
	} else {
		s->pending = false;
		if (err)
			return err;
	}
	s->used = true;
	t->count++;
	return 0;
}

static int bench_check(struct bench_slot *slots, unsigned int depth,
		       unsigned int len, bool decrypt)
{
	unsigned int i;
	int err;

	for (i = 0; i < depth; i++) {
		struct bench_slot *s = &slots[i];

		err = bench_buffer_check(&s->src, decrypt ? s->cipher : s->plain, len);
		if (!err && s->used)
			err = bench_buffer_check(&s->dst, decrypt ? s->plain : s->cipher, len);
		if (!err && s->used && memcmp(s->iv, s->next, sizeof(s->iv)))
			err = -EPROTO;
		if (err) {
			pr_err("verification failed: slot=%u len=%u decrypt=%u err=%d\n",
			       i, len, decrypt, err);
			return err;
		}
	}
	return 0;
}

static int bench_preflight(struct bench_slot *slots, unsigned int depth, bool decrypt)
{
	struct bench_totals t = {};
	unsigned int i;
	int err = 0, ret;

	for (i = 0; i < depth; i++) {
		err = bench_submit(&slots[i], decrypt, &t);
		if (err)
			break;
	}
	ret = bench_drain(slots, depth);
	return err ?: ret;
}

static int bench_window(struct bench_slot *slots, unsigned int depth, bool decrypt,
			unsigned int duration, struct bench_totals *t)
{
	u64 start, end;
	unsigned int i = 0;
	int err, ret;

	memset(t, 0, sizeof(*t));
	for (i = 0; i < depth; i++) {
		slots[i].used = false;
		memset(slots[i].dst.data, BENCH_POISON, slots[i].dst.extent);
	}
	i = 0;
	start = ktime_get_ns();
	end = start + (u64)duration * NSEC_PER_MSEC;
	do {
		/* Recycle in submission order; never modify an outstanding request. */
		err = bench_wait(&slots[i]);
		if (err)
			break;
		if (fatal_signal_pending(current)) {
			err = -EINTR;
			break;
		}
		if (t->count && ktime_get_ns() >= end)
			break;
		err = bench_submit(&slots[i], decrypt, t);
		if (err)
			break;
		i = (i + 1) % depth;
		if (!(t->count & 255))
			cond_resched();
	} while (ktime_get_ns() < end);
	ret = bench_drain(slots, depth);
	t->elapsed = ktime_get_ns() - start;
	return err ?: ret;
}

static int bench_case(struct crypto_skcipher *tfm, struct crypto_sync_skcipher *ref,
		      unsigned int len, unsigned int depth, bool decrypt)
{
	struct bench_slot *slots;
	struct bench_totals t;
	unsigned int i, trial = 0;
	int err;

	slots = kcalloc(depth, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return -ENOMEM;
	for (i = 0; i < depth; i++) {
		err = bench_slot_init(&slots[i], tfm, ref, len, i, decrypt);
		if (err)
			goto out;
	}
	err = bench_preflight(slots, depth, decrypt);
	if (!err)
		err = bench_check(slots, depth, len, decrypt);
	if (err)
		goto out;
	if (warmup_ms) {
		err = bench_window(slots, depth, decrypt, warmup_ms, &t);
		if (!err)
			err = bench_check(slots, depth, len, decrypt);
		if (err)
			goto out;
	}
	for (trial = 1; trial <= runs; trial++) {
		u64 bytes, rate;

		err = bench_window(slots, depth, decrypt, duration_ms, &t);
		if (!err)
			err = bench_check(slots, depth, len, decrypt);
		if (err)
			goto out;
		bytes = t.count * len;
		rate = mul_u64_u64_div_u64(bytes, 8 * NSEC_PER_MSEC, t.elapsed);
		pr_info("result,%s,%u,%u,%u,%u,%llu,%llu,%llu,%llu.%03llu,%llu,%llu,%llu\n",
			decrypt ? "decrypt" : "encrypt", key_bits, len, depth, trial,
			t.count, bytes, t.elapsed, div_u64(rate, 1000), rate % 1000,
			div64_u64(t.elapsed, t.count), t.async, t.backlog);
		rows++;
		requests += t.count;
	}
out:
	if (err)
		pr_err("case failed: len=%u depth=%u decrypt=%u run=%u err=%d\n",
		       len, depth, decrypt, trial, err);
	bench_slots_free(slots, depth);
	return err;
}

static int __init eip93_ctr_bench_init(void)
{
	struct crypto_sync_skcipher *ref;
	struct crypto_skcipher *tfm;
	u8 key[AES_MAX_KEY_SIZE];
	bool encrypt, decrypt;
	unsigned int i, j;
	int err;

	encrypt = !strcmp(direction, "encrypt") || !strcmp(direction, "both");
	decrypt = !strcmp(direction, "decrypt") || !strcmp(direction, "both");
	if ((!encrypt && !decrypt) || !num_lengths || !num_depths ||
	    (key_bits != 128 && key_bits != 192 && key_bits != 256) ||
	    !duration_ms || duration_ms > 60000 || warmup_ms > 60000 ||
	    !runs || runs > 20 || !timeout_ms || timeout_ms > 60000)
		return -EINVAL;
	for (i = 0; i < num_lengths; i++)
		if (!lengths[i] || lengths[i] > BENCH_MAX_SIZE)
			return -EINVAL;
	for (i = 0; i < num_depths; i++)
		if (!depths[i] || depths[i] > BENCH_MAX_DEPTH)
			return -EINVAL;
	alignment = max_t(unsigned int, 128, dma_get_cache_alignment());
	if (!is_power_of_2(alignment) || alignment > PAGE_SIZE)
		return -EINVAL;
	tfm = crypto_alloc_skcipher(driver, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	if (strcmp(crypto_tfm_alg_name(crypto_skcipher_tfm(tfm)), "ctr(aes)") ||
	    crypto_skcipher_ivsize(tfm) != AES_BLOCK_SIZE) {
		err = -EINVAL;
		goto free_tfm;
	}
	ref = crypto_alloc_sync_skcipher("ctr(aes-generic)", 0, 0);
	if (IS_ERR(ref)) {
		err = PTR_ERR(ref);
		goto free_tfm;
	}
	for (i = 0; i < sizeof(key); i++)
		key[i] = i ^ (seed >> ((i & 3) * 8));
	err = crypto_skcipher_setkey(tfm, key, key_bits / 8);
	if (!err)
		err = crypto_sync_skcipher_setkey(ref, key, key_bits / 8);
	memzero_explicit(key, sizeof(key));
	if (err)
		goto free_ref;
	pr_info("target=%s reference=ctr(aes-generic) key_bits=%u seed=%u alignment=%u single_sg=1 out_of_place=1\n",
		crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), key_bits, seed, alignment);
	pr_info("duration_ms=%u warmup_ms=%u runs=%u verify=preflight+final_slots; intermediate outputs are reused\n",
		duration_ms, warmup_ms, runs);
	pr_info("kind,direction,key_bits,length,depth,run,requests,bytes,elapsed_ns,mbps,ns_per_request,async,backlogs\n");
	for (i = 0; i < num_lengths; i++) {
		for (j = 0; j < num_depths; j++) {
			if (encrypt) {
				err = bench_case(tfm, ref, lengths[i], depths[j], false);
				if (err)
					goto free_ref;
			}
			if (decrypt) {
				err = bench_case(tfm, ref, lengths[i], depths[j], true);
				if (err)
					goto free_ref;
			}
		}
	}
	pr_info("PASS rows=%u timed_requests=%llu\n", rows, requests);
free_ref:
	crypto_free_sync_skcipher(ref);
free_tfm:
	crypto_free_skcipher(tfm);
	return err;
}

static void __exit eip93_ctr_bench_exit(void)
{
}

module_init(eip93_ctr_bench_init);
module_exit(eip93_ctr_bench_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aligned CTR length and queue-depth throughput benchmark");
