// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Direct Crypto API differential tests for the EIP93 driver.
 * Layouts, poison checks and streaming tests follow crypto/testmgr.c.
 * See vectors.h for known-answer vectors taken from crypto/testmgr.h.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/authenc.h>
#include <crypto/gcm.h>
#include <crypto/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>

#define GUARD 32
#define POISON 0xa5
#define ALIGNMENT 128
#define MAX_KEY 320
#define MAX_IV 16

static char *alg;
module_param(alg, charp, 0444);
MODULE_PARM_DESC(alg, "Exact generic algorithm name; unset tests all EIP93 algorithms");
static char *group = "all";
module_param(group, charp, 0444);
MODULE_PARM_DESC(group, "all, skcipher, hash or aead");
static bool software;
module_param(software, bool, 0444);
MODULE_PARM_DESC(software, "Validate the harness using software targets; does not test EIP93");
static bool software_async;
module_param(software_async, bool, 0444);
MODULE_PARM_DESC(software_async, "With software=1, wrap targets in cryptd to exercise async completion");
static bool verbose;
module_param(verbose, bool, 0444);
MODULE_PARM_DESC(verbose, "Log each case before submitting it");
static bool stop_on_failure;
module_param(stop_on_failure, bool, 0444);
static unsigned int seed;
module_param(seed, uint, 0444);
MODULE_PARM_DESC(seed, "Replay seed; 0 selects and logs a fresh random seed on each load");
static unsigned int max_size = 65536;
module_param(max_size, uint, 0444);
MODULE_PARM_DESC(max_size, "Largest matrix message, 0..65536 bytes");
static int case_id = -1;
module_param(case_id, int, 0444);
MODULE_PARM_DESC(case_id, "Run one matrix case per selected algorithm; -1 runs all and KATs");
static unsigned int lengths[32];
static unsigned int num_lengths;
module_param_array(lengths, uint, &num_lengths, 0444);
MODULE_PARM_DESC(lengths, "Override payload lengths for case_id, preserving its other parameters");
static int aad_length = -1;
module_param(aad_length, int, 0444);
MODULE_PARM_DESC(aad_length, "AEAD case replay AAD length, 0..1024 (-1 keeps the case value)");
static int src_offset = -1;
module_param(src_offset, int, 0444);
MODULE_PARM_DESC(src_offset, "Flat out-of-place cipher replay source offset, 0..127 (-1 keeps layout)");
static int dst_offset = -1;
module_param(dst_offset, int, 0444);
MODULE_PARM_DESC(dst_offset, "Flat out-of-place cipher replay destination offset, 0..127 (-1 keeps layout)");
static bool pad_src;
module_param(pad_src, bool, 0444);
MODULE_PARM_DESC(pad_src, "Pad the replay source SG extent to 128 bytes, retaining the crypt length");
static bool pad_dst;
module_param(pad_dst, bool, 0444);
MODULE_PARM_DESC(pad_dst, "Pad the replay destination SG extent to 128 bytes, retaining the crypt length");
static unsigned int repeats = 1;
module_param(repeats, uint, 0444);
MODULE_PARM_DESC(repeats, "Repeat each case 1..10000 times with identical input (default 1)");
static unsigned int timeout_ms = 10000;
module_param(timeout_ms, uint, 0444);
MODULE_PARM_DESC(timeout_ms, "Warn after this wait, then retain request storage until completion");
static unsigned long tests, failures, skipped, async_ops;
module_param(tests, ulong, 0444);
module_param(failures, ulong, 0444);
module_param(skipped, ulong, 0444);
module_param(async_ops, ulong, 0444);

enum kind { SKCIPHER, HASH, AEAD };
struct algorithm {
	const char *name, *driver, *reference;
	enum kind kind;
	u8 keys[3], nkeys, block, ivsize, digest;
	bool hmac, rfc3686, gcm;
};

#include "algorithms.h"
#include "vectors.h"

enum shape { FLAT, EVEN, UNEVEN, CROSS_PAGE, BLOCKS };
struct layout {
	const char *name;
	u8 src_shape, dst_shape, src_off, dst_off, key_off, iv_off, inplace;
};

/* inplace=2 uses distinct SG tables referring to the same data pages. */
static const struct layout layouts[] = {
	{ "aligned-out", FLAT, FLAT, 0, 0, 0, 0, 0 },
	{ "aligned-in", FLAT, FLAT, 0, 0, 0, 0, 1 },
	{ "alias-sglists", EVEN, EVEN, 0, 0, 1, 1, 2 },
	{ "byte-offset", FLAT, FLAT, 1, 3, 1, 3, 0 },
	{ "word-not-cache", FLAT, FLAT, 4, 4, 3, 7, 1 },
	{ "different-splits", EVEN, UNEVEN, 0, 15, 7, 15, 0 },
	{ "uneven-in", UNEVEN, UNEVEN, 7, 7, 15, 1, 1 },
	{ "page-crossing", CROSS_PAGE, CROSS_PAGE, 0, 0, 31, 3, 0 },
	{ "cache-edge", FLAT, EVEN, 63, 127, 63, 7, 0 },
	{ "many-src", BLOCKS, FLAT, 0, 0, 0, 0, 0 },
	{ "many-dst", FLAT, BLOCKS, 0, 0, 0, 15, 0 },
};

static const unsigned int sizes[] = {
	0, 1, 7, 8, 15, 16, 17, 31, 32, 55, 56, 63, 64, 65,
	127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
	4095, 4096, 4097, 16368, 16383, 16384, 16385, 32768, 65536,
};

static const unsigned int hmac_keys[] = { 0, 1, 16, 63, 64, 65, 131, 256 };
static const unsigned int aad_sizes[] = { 0, 1, 3, 4, 5, 8, 12, 13, 16, 63, 64, 65 };
static const unsigned int gcm_tags[] = { 4, 8, 12, 13, 14, 15, 16 };

struct params {
	const struct algorithm *a;
	unsigned int id, len, keylen, authkeylen, aad, tag, iv_pattern, stream;
	const struct layout *layout;
	const struct known_answer *kat;
	bool pad_src, pad_dst;
};

static struct crypto_sync_skcipher *payload_cipher;

struct buffer {
	u8 *allocation, *data;
	unsigned int len;
};

static int buffer_alloc(struct buffer *b, unsigned int len, unsigned int off)
{
	b->allocation = kmalloc(len + 2 * GUARD + 2 * ALIGNMENT, GFP_KERNEL);
	if (!b->allocation)
		return -ENOMEM;
	b->data = PTR_ALIGN(b->allocation + GUARD, ALIGNMENT) + off;
	b->len = len;
	memset(b->data - GUARD, POISON, len + 2 * GUARD);
	return 0;
}

static int buffer_check(const struct buffer *b)
{
	if (memchr_inv(b->data - GUARD, POISON, GUARD) ||
	    memchr_inv(b->data + b->len, POISON, GUARD))
		return -EOVERFLOW;
	return 0;
}

static void buffer_free(struct buffer *b)
{
	kfree_sensitive(b->allocation);
}

struct piece {
	u8 *allocation, *data;
	unsigned int order, len, mapped_len;
	struct scatterlist *sg;
	unsigned long page_link;
	unsigned int offset;
};

struct scatter_buffer {
	struct sg_table table;
	struct piece *pieces;
	unsigned int nents, len;
};

static unsigned int piece_size(unsigned int len, unsigned int remaining,
			       enum shape shape, unsigned int i)
{
	static const unsigned int uneven[] = { 1, 15, 17, 63, 64, 65, 1024 };
	unsigned int n = remaining;

	switch (shape) {
	case EVEN:
		n = DIV_ROUND_UP(len, 2);
		break;
	case UNEVEN:
		n = uneven[i % ARRAY_SIZE(uneven)];
		break;
	case CROSS_PAGE:
		n = 1024;
		break;
	case BLOCKS:
		n = 64;
		break;
	default:
		break;
	}
	return min(n, remaining);
}

static void scatter_free(struct scatter_buffer *b)
{
	unsigned int i;

	for (i = 0; i < b->nents; i++)
		if (b->pieces[i].allocation)
			free_pages((unsigned long)b->pieces[i].allocation,
				   b->pieces[i].order);
	kfree(b->pieces);
	if (b->table.sgl)
		sg_free_table(&b->table);
}

static int scatter_alloc(struct scatter_buffer *b, unsigned int len,
			 enum shape shape, unsigned int off, bool pad)
{
	struct scatterlist *sg;
	unsigned int n = 0, left = len, i;
	int err;

	/* Padding between SG entries would change the logical byte stream. */
	if (pad && shape != FLAT)
		return -EINVAL;

	do {
		left -= piece_size(len, left, shape, n++);
	} while (left);
	err = sg_alloc_table(&b->table, n, GFP_KERNEL);
	if (err)
		return err;
	b->pieces = kcalloc(n, sizeof(*b->pieces), GFP_KERNEL);
	if (!b->pieces) {
		sg_free_table(&b->table);
		b->table.sgl = NULL;
		return -ENOMEM;
	}
	b->nents = n;
	b->len = len;
	left = len;
	for_each_sg(b->table.sgl, sg, n, i) {
		struct piece *p = &b->pieces[i];
		unsigned int offset = ALIGNMENT + off;

		p->len = piece_size(len, left, shape, i);
		p->mapped_len = pad ? ALIGN(p->len, ALIGNMENT) : p->len;
		if (shape == CROSS_PAGE)
			offset = PAGE_SIZE - (i & 1 ? 7 : 32);
		p->order = get_order(offset + p->mapped_len + GUARD);
		p->allocation = (void *)__get_free_pages(GFP_KERNEL, p->order);
		if (!p->allocation)
			return -ENOMEM;
		p->data = p->allocation + offset;
		memset(p->data - GUARD, POISON, p->mapped_len + 2 * GUARD);
		sg_set_buf(sg, p->data, p->mapped_len);
		p->sg = sg;
		p->page_link = sg->page_link;
		p->offset = sg->offset;
		left -= p->len;
	}
	return 0;
}

static int scatter_check(const struct scatter_buffer *b)
{
	unsigned int i;

	for (i = 0; i < b->nents; i++) {
		const struct piece *p = &b->pieces[i];

		if (p->sg->page_link != p->page_link ||
		    p->sg->offset != p->offset || p->sg->length != p->mapped_len ||
		    memchr_inv(p->data - GUARD, POISON, GUARD) ||
		    memchr_inv(p->data + p->len, POISON, p->mapped_len - p->len + GUARD))
			return -EOVERFLOW;
	}
	return 0;
}

static int scatter_compare(const struct scatter_buffer *b, const u8 *expected,
			   unsigned int offset, unsigned int len,
			   const char *what, unsigned int tag_start)
{
	unsigned int first = UINT_MAX, last = 0, pos = offset;
	unsigned int data_bytes = 0, tag_bytes = 0, i;
	u8 first_expected = 0, first_actual = 0;

	for (i = 0; i < b->nents && len; i++) {
		const struct piece *p = &b->pieces[i];
		unsigned int n, j;

		if (offset >= p->len) {
			offset -= p->len;
			continue;
		}
		n = min(len, p->len - offset);
		if (memcmp(p->data + offset, expected, n)) {
			for (j = 0; j < n; j++) {
				if (p->data[offset + j] == expected[j])
					continue;
				if (first == UINT_MAX) {
					first = pos + j;
					first_expected = expected[j];
					first_actual = p->data[offset + j];
				}
				last = pos + j;
				if (pos + j >= tag_start)
					tag_bytes++;
				else
					data_bytes++;
			}
		}
		expected += n;
		len -= n;
		pos += n;
		offset = 0;
	}
	if (len)
		return -EINVAL;
	if (first == UINT_MAX)
		return 0;
	pr_err("%s mismatch: first=%u last=%u data_bytes=%u tag_bytes=%u expected=%02x actual=%02x\n",
	       what, first, last, data_bytes, tag_bytes, first_expected, first_actual);
	return -EBADMSG;
}

struct io_buffers {
	struct scatter_buffer src, dst;
	struct sg_table alias;
	struct scatterlist *output;
	struct buffer iv;
	bool inplace;
};

static void io_free(struct io_buffers *io)
{
	if (io->alias.sgl)
		sg_free_table(&io->alias);
	scatter_free(&io->src);
	scatter_free(&io->dst);
	buffer_free(&io->iv);
}

static int io_alloc(struct io_buffers *io, const struct params *p,
		    const u8 *input, unsigned int inlen, unsigned int outlen,
		    const u8 *iv)
{
	const struct layout *l = p->layout;
	struct scatterlist *sg;
	unsigned int i;
	int err;

	io->inplace = l->inplace;
	err = scatter_alloc(&io->src, l->inplace ? max(inlen, outlen) : inlen,
			    l->src_shape, l->src_off, p->pad_src);
	if (err)
		return err;
	sg_copy_from_buffer(io->src.table.sgl, io->src.nents, input, inlen);
	io->output = io->src.table.sgl;
	if (!l->inplace) {
		err = scatter_alloc(&io->dst, outlen, l->dst_shape, l->dst_off, p->pad_dst);
		if (err)
			return err;
		io->output = io->dst.table.sgl;
		/* The AEAD API does not require copying AAD to a separate dst. */
		if (p->aad)
			sg_copy_from_buffer(io->output, io->dst.nents, input, p->aad);
	} else if (l->inplace == 2) {
		err = sg_alloc_table(&io->alias, io->src.nents, GFP_KERNEL);
		if (err)
			return err;
		for_each_sg(io->alias.sgl, sg, io->src.nents, i)
			sg_set_buf(sg, io->src.pieces[i].data, io->src.pieces[i].len);
		io->output = io->alias.sgl;
	}
	err = buffer_alloc(&io->iv, p->a->ivsize, l->iv_off);
	if (!err && p->a->ivsize)
		memcpy(io->iv.data, iv, p->a->ivsize);
	return err;
}

static int io_check(struct io_buffers *io, const struct params *p,
		    const u8 *input, unsigned int inlen, const u8 *output,
		    unsigned int outlen, bool check_output, bool decrypt)
{
	struct scatterlist *sg;
	unsigned int i;
	int err = scatter_check(&io->src) ?: buffer_check(&io->iv);

	if (io->alias.sgl) {
		for_each_sg(io->alias.sgl, sg, io->src.nents, i) {
			const struct piece *piece = &io->src.pieces[i];

			if (sg_page(sg) != virt_to_page(piece->data) ||
			    sg->offset != offset_in_page(piece->data) || sg->length != piece->len)
				err = -EOVERFLOW;
		}
	}
	if (!err && !io->inplace)
		err = scatter_check(&io->dst) ?:
		      scatter_compare(&io->src, input, 0, inlen, "source", UINT_MAX);
	if (!err && check_output)
		err = scatter_compare(io->inplace ? &io->src : &io->dst,
				      output + p->aad, p->aad, outlen - p->aad,
				      decrypt ? "decrypt output" : "encrypt output",
				      p->a->kind == AEAD && !decrypt ? p->aad + p->len : UINT_MAX);
	return err;
}

static void fill(u8 *out, unsigned int len, u32 salt)
{
	u32 x = seed ^ salt ^ 0x9e3779b9;
	unsigned int i;

	for (i = 0; i < len; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		out[i] = x;
	}
}

static int payload_init(void)
{
	/* Public, fixed test key. The seed selects a different counter stream. */
	static const u8 key[AES_KEYSIZE_128] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	int err;

	/* An explicit software driver and the sync API exclude EIP93/cryptd. */
	payload_cipher = crypto_alloc_sync_skcipher("ctr(aes-generic)", 0, 0);
	if (IS_ERR(payload_cipher))
		return PTR_ERR(payload_cipher);
	err = crypto_sync_skcipher_setkey(payload_cipher, key, sizeof(key));
	if (err) {
		crypto_free_sync_skcipher(payload_cipher);
		return err;
	}
	pr_info("payload generator=%s (synchronous)\n",
		crypto_tfm_alg_driver_name(crypto_sync_skcipher_tfm(payload_cipher)));
	return 0;
}

static int payload_fill(const struct params *p, u8 *out, unsigned int len,
			bool malformed)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, payload_cipher);
	struct scatterlist sg;
	__be32 iv[AES_BLOCK_SIZE / sizeof(__be32)] = {
		cpu_to_be32(seed),
		cpu_to_be32(((p->a - algorithms) << 1) | malformed),
		cpu_to_be32(p->id),
		0,
	};
	int err;

	if (!len)
		return 0;
	/* Reset per case: filtering or changing execution order preserves replay. */
	memset(out, 0, len);
	sg_init_one(&sg, out, len);
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, len, (u8 *)iv);
	err = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
	if (err)
		pr_err("software payload generation failed: %d\n", err);
	return err;
}

static void make_iv(const struct params *p, u8 *iv)
{
	fill(iv, MAX_IV, p->id + 99);
	if (!p->iv_pattern) {
		memset(iv, 0, MAX_IV);
	} else if (p->iv_pattern == 2) {
		memset(iv + 12, 0xff, 4);
		iv[15] = 0xfe;
	} else if (p->iv_pattern == 3) {
		memset(iv, 0xff, MAX_IV);
	}
	if (p->kat && p->kat->iv)
		memcpy(iv, p->kat->iv, p->a->ivsize);
}

static int wait_req(int err, struct crypto_wait *wait)
{
	bool expired = false;

	if (err != -EINPROGRESS && err != -EBUSY)
		return err;
	async_ops++;
	if (!wait_for_completion_timeout(&wait->completion,
					 msecs_to_jiffies(timeout_ms))) {
		pr_err("request stalled; retaining live buffers and waiting for completion\n");
		expired = true;
		/* Crypto API has no cancellation operation. Never free live DMA. */
		wait_for_completion(&wait->completion);
	}
	err = wait->err;
	reinit_completion(&wait->completion);
	return expired ? -ETIMEDOUT : err;
}

static u32 req_flags(const struct params *p)
{
	return CRYPTO_TFM_REQ_MAY_BACKLOG |
	       (p->id & 1 ? CRYPTO_TFM_REQ_MAY_SLEEP : 0);
}

static void describe(const struct params *p, const char *status, int err,
		     unsigned int trial)
{
	pr_info("%s alg=%s case=%u kat=%u len=%u key=%u hkey=%u aad=%u tag=%u layout=%s src_off=%u dst_off=%u pad_src=%u pad_dst=%u iv=%u stream=%u seed=%u repeat=%u/%u err=%d\n",
		status, p->a->name, p->id, !!p->kat, p->len, p->keylen,
		p->authkeylen, p->aad, p->tag, p->layout->name,
		p->layout->src_off, p->layout->dst_off, p->pad_src, p->pad_dst,
		p->iv_pattern, p->stream, seed, trial, repeats, err);
}

struct transforms {
	struct crypto_skcipher *sk, *sk_ref;
	struct crypto_ahash *hash;
	struct crypto_shash *hash_ref;
	struct crypto_aead *ae, *ae_ref;
};

static void transforms_free(struct transforms *t)
{
	if (!IS_ERR_OR_NULL(t->sk))
		crypto_free_skcipher(t->sk);
	if (!IS_ERR_OR_NULL(t->sk_ref))
		crypto_free_skcipher(t->sk_ref);
	if (!IS_ERR_OR_NULL(t->hash))
		crypto_free_ahash(t->hash);
	if (!IS_ERR_OR_NULL(t->hash_ref))
		crypto_free_shash(t->hash_ref);
	if (!IS_ERR_OR_NULL(t->ae))
		crypto_free_aead(t->ae);
	if (!IS_ERR_OR_NULL(t->ae_ref))
		crypto_free_aead(t->ae_ref);
}

static int transforms_alloc(struct transforms *t, const struct algorithm *a)
{
	const char *target = software ? a->reference : a->driver;
	char async_name[CRYPTO_MAX_ALG_NAME];
	const char *actual, *ref;
	int err;

	if (software_async) {
		if (snprintf(async_name, sizeof(async_name), "cryptd(%s)", target) >=
		    sizeof(async_name))
			return -ENAMETOOLONG;
		target = async_name;
	}
	switch (a->kind) {
	case SKCIPHER:
		t->sk = crypto_alloc_skcipher(target, 0, 0);
		if (IS_ERR(t->sk))
			return PTR_ERR(t->sk);
		t->sk_ref = crypto_alloc_skcipher(a->reference, 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(t->sk_ref))
			goto missing_ref;
		actual = crypto_skcipher_driver_name(t->sk);
		ref = crypto_skcipher_driver_name(t->sk_ref);
		break;
	case HASH:
		t->hash = crypto_alloc_ahash(target, 0, 0);
		if (IS_ERR(t->hash))
			return PTR_ERR(t->hash);
		t->hash_ref = crypto_alloc_shash(a->reference, 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(t->hash_ref))
			goto missing_ref;
		actual = crypto_ahash_driver_name(t->hash);
		ref = crypto_shash_driver_name(t->hash_ref);
		break;
	case AEAD:
		t->ae = crypto_alloc_aead(target, 0, 0);
		if (IS_ERR(t->ae))
			return PTR_ERR(t->ae);
		t->ae_ref = crypto_alloc_aead(a->reference, 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(t->ae_ref))
			goto missing_ref;
		actual = crypto_aead_driver_name(t->ae);
		ref = crypto_aead_driver_name(t->ae_ref);
		break;
	default:
		return -EINVAL;
	}
	/* Never let generic-name priority turn the oracle into the device. */
	if (strstr(ref, "eip93") || (!software && !strstr(actual, "eip93")))
		return -EINVAL;
	pr_info("alg=%s target=%s reference=%s\n", a->name, actual, ref);
	return 0;

missing_ref:
	err = a->kind == SKCIPHER ? PTR_ERR(t->sk_ref) :
	      a->kind == HASH ? PTR_ERR(t->hash_ref) : PTR_ERR(t->ae_ref);
	pr_err("cannot allocate software reference %s: %d\n", a->reference, err);
	/* A missing reference is a test setup failure, not missing hardware. */
	return err == -ENOENT || err == -ENODEV ? -ENOPKG : err;
}

static void make_key(const struct params *p, u8 *key)
{
	unsigned int prefix = 0;

	if (p->a->kind == AEAD && !p->a->gcm) {
		struct {
			struct rtattr attr;
			struct crypto_authenc_key_param param;
		} header = {
			.attr.rta_len = RTA_LENGTH(sizeof(header.param)),
			.attr.rta_type = CRYPTO_AUTHENC_KEYA_PARAM,
			.param.enckeylen = cpu_to_be32(p->keylen),
		};

		prefix = sizeof(header);
		memcpy(key, &header, prefix);
		fill(key + prefix, p->authkeylen, p->id + 301);
		if (p->kat && p->authkeylen)
			memcpy(key + prefix, p->kat->authkey, p->authkeylen);
		prefix += p->authkeylen;
	}
	fill(key + prefix, p->keylen, p->id + 11);
	if (p->kat && p->keylen)
		memcpy(key + prefix, p->kat->key, p->keylen);
}

static unsigned int key_size(const struct params *p)
{
	return p->keylen + (p->a->kind == AEAD && !p->a->gcm ?
		RTA_SPACE(sizeof(struct crypto_authenc_key_param)) + p->authkeylen : 0);
}

static void diagnose_cipher_block(struct transforms *t, const struct params *p,
				  struct io_buffers *io, const u8 *input,
				  const u8 *expected, bool decrypt)
{
	unsigned int block = p->a->block, offset, i;
	unsigned int nents = io->inplace ? io->src.nents : io->dst.nents;
	bool cbc = !strncmp(p->a->name, "cbc(", 4);
	const char *mode = cbc ? "CBC" : "ECB";
	struct skcipher_request *req;
	struct scatterlist src, dst;
	u8 iv[MAX_IV];
	u8 *actual, *recovered;
	u32 different = 0;
	int err;

	if ((!cbc && strncmp(p->a->name, "ecb(", 4)) || !block ||
	    block > AES_BLOCK_SIZE || p->len % block)
		return;
	actual = kmalloc(2 * block, GFP_KERNEL);
	if (!actual)
		return;
	recovered = actual + block;
	/* Inspect only the first bad block; diagnostics must stay bounded. */
	for (offset = 0; offset < p->len; offset += block) {
		if (sg_pcopy_to_buffer(io->output, nents, actual, block, offset) != block)
			goto free_data;
		if (memcmp(actual, expected + offset, block))
			break;
	}
	if (offset == p->len)
		goto free_data;
	pr_err("%s %s: block=%u expected=%*phN actual=%*phN\n",
	       mode, decrypt ? "plaintext" : "ciphertext",
	       offset, block, expected + offset, block, actual);
	if (cbc) {
		/* Invert one block using its original chaining value. */
		if (!offset)
			make_iv(p, iv);
		else if (decrypt)
			memcpy(iv, input + offset - block, block);
		else if (sg_pcopy_to_buffer(io->output, nents, iv, block,
					    offset - block) != block)
			goto free_data;
		pr_err("CBC chain: block=%u value=%*phN\n", offset, block, iv);
	}
	req = skcipher_request_alloc(t->sk_ref, GFP_KERNEL);
	if (!req)
		goto free_data;
	sg_init_one(&src, actual, block);
	sg_init_one(&dst, recovered, block);
	/* sk_ref is an explicitly selected synchronous software implementation. */
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);
	skcipher_request_set_crypt(req, &src, &dst, block, cbc ? iv : NULL);
	err = decrypt ? crypto_skcipher_encrypt(req) : crypto_skcipher_decrypt(req);
	if (err) {
		pr_err("%s inverse failed: %d\n", mode, err);
	} else {
		for (i = 0; i < block; i++)
			if (recovered[i] != input[offset + i])
				different |= BIT(i);
		pr_err("%s inverse: operation=%s block=%u different=%#x expected_input=%*phN recovered_input=%*phN\n",
		       mode, decrypt ? "encrypt" : "decrypt", offset, different,
		       block, input + offset, block, recovered);
	}
	skcipher_request_free(req);
free_data:
	kfree_sensitive(actual);
}

/* Every request is drained before its storage, callback or transform is freed. */
static int cipher_op(struct transforms *t, const struct params *p, bool reference,
		     bool decrypt, const u8 *input, const u8 *expected,
		     int expected_err, u8 *result, u8 *result_iv)
{
	DECLARE_CRYPTO_WAIT(wait);
	struct io_buffers io = {};
	struct skcipher_request *skreq = NULL;
	struct aead_request *aereq = NULL;
	bool aead = p->a->kind == AEAD;
	unsigned int inlen = p->aad + p->len + (aead && decrypt ? p->tag : 0);
	unsigned int outlen = p->aad + p->len + (aead && !decrypt ? p->tag : 0);
	u8 iv[MAX_IV];
	int err, check;

	make_iv(p, iv);
	err = io_alloc(&io, p, input, inlen, outlen, iv);
	if (err)
		goto out;
	if (aead) {
		aereq = aead_request_alloc(reference ? t->ae_ref : t->ae, GFP_KERNEL);
		if (!aereq) {
			err = -ENOMEM;
			goto out;
		}
		aead_request_set_callback(aereq, req_flags(p), crypto_req_done, &wait);
		aead_request_set_crypt(aereq, io.src.table.sgl, io.output,
				       inlen - p->aad, io.iv.data);
		aead_request_set_ad(aereq, p->aad);
		err = wait_req(decrypt ? crypto_aead_decrypt(aereq) :
					 crypto_aead_encrypt(aereq), &wait);
		check = aereq->src != io.src.table.sgl || aereq->dst != io.output ||
			aereq->cryptlen != inlen - p->aad || aereq->iv != io.iv.data ||
			aereq->assoclen != p->aad;
	} else {
		skreq = skcipher_request_alloc(reference ? t->sk_ref : t->sk, GFP_KERNEL);
		if (!skreq) {
			err = -ENOMEM;
			goto out;
		}
		skcipher_request_set_callback(skreq, req_flags(p), crypto_req_done, &wait);
		skcipher_request_set_crypt(skreq, io.src.table.sgl, io.output,
					   p->len, io.iv.data);
		err = wait_req(decrypt ? crypto_skcipher_decrypt(skreq) :
					 crypto_skcipher_encrypt(skreq), &wait);
		check = skreq->src != io.src.table.sgl || skreq->dst != io.output ||
			skreq->cryptlen != p->len || skreq->iv != io.iv.data;
	}
	/* Authenc may authenticate before noticing an invalid block length. */
	if (aead && decrypt && expected_err == -EINVAL && err == -EBADMSG)
		err = -EINVAL;
	if (err != expected_err) {
		pr_err("%s %s returned %d, expected %d\n",
		       reference ? "reference" : "target", decrypt ? "decrypt" : "encrypt",
		       err, expected_err);
		err = err ?: -EPROTO;
	} else {
		err = 0;
	}
	if (check)
		err = -EOVERFLOW;
	check = io_check(&io, p, input, inlen, expected, outlen,
			 !err && !expected_err && expected, decrypt);
	if (!check && !err && expected_err == -EBADMSG && p->a->gcm &&
	    !software && !reference && io.inplace)
		check = scatter_compare(&io.src, input, 0, inlen,
					"rejected GCM input", UINT_MAX);
	if (check == -EBADMSG && !reference && !aead &&
	    !err && !expected_err && expected)
		diagnose_cipher_block(t, p, &io, input, expected, decrypt);
	if (check) {
		pr_err("%s %s data/SG/guard check failed: %d\n",
		       reference ? "reference" : "target", decrypt ? "decrypt" : "encrypt",
		       check);
		err = check;
	}
	if (!err && !expected_err) {
		if (result)
			sg_copy_to_buffer(io.output, io.inplace ? io.src.nents : io.dst.nents,
					  result, outlen);
		if (result_iv)
			memcpy(result_iv, io.iv.data, p->a->ivsize);
	}
out:
	aead_request_free(aereq);
	skcipher_request_free(skreq);
	io_free(&io);
	return err;
}

static int aead_key(struct crypto_aead *tfm, const struct params *p,
		    const u8 *key, unsigned int len)
{
	int err;

	/* In particular, long-HMAC-key normalization must not depend on authsize. */
	if (p->id & 1) {
		err = crypto_aead_setkey(tfm, key, len);
		return err ?: crypto_aead_setauthsize(tfm, p->tag);
	}
	err = crypto_aead_setauthsize(tfm, p->tag);
	return err ?: crypto_aead_setkey(tfm, key, len);
}

static int cipher_case(struct transforms *t, const struct params *p)
{
	struct params ref = *p;
	struct buffer key = {};
	u8 key_copy[MAX_KEY], iv_ref[MAX_IV], iv_out[MAX_IV];
	u8 *plain = NULL, *cipher = NULL;
	unsigned int inlen = p->aad + p->len;
	unsigned int outlen = inlen + (p->a->kind == AEAD ? p->tag : 0);
	int err, invalid = p->len % p->a->block ? -EINVAL : 0;
	unsigned int i;

	ref.layout = &layouts[0];
	ref.pad_src = false;
	ref.pad_dst = false;
	err = buffer_alloc(&key, key_size(p), p->layout->key_off);
	if (err)
		return err;
	make_key(p, key.data);
	memcpy(key_copy, key.data, key.len);
	plain = kmalloc(max(1U, inlen), GFP_KERNEL);
	cipher = kmalloc(max(1U, outlen), GFP_KERNEL);
	if (!plain || !cipher) {
		err = -ENOMEM;
		goto out;
	}
	if (p->kat) {
		memcpy(plain + p->aad, p->kat->input, p->len);
		if (p->aad)
			memcpy(plain, p->kat->aad, p->aad);
	} else {
		err = payload_fill(p, plain, inlen, false);
		if (err)
			goto out;
	}
	if (p->a->kind == AEAD) {
		err = aead_key(t->ae_ref, p, key.data, key.len);
		if (!err)
			err = aead_key(t->ae, p, key.data, key.len);
	} else {
		err = crypto_skcipher_setkey(t->sk_ref, key.data, key.len);
		if (!err)
			err = crypto_skcipher_setkey(t->sk, key.data, key.len);
	}
	if (err) {
		pr_err("setkey/setauthsize failed: %d\n", err);
		goto out;
	}
	err = cipher_op(t, &ref, true, false, plain, NULL, invalid, cipher, iv_ref);
	if (err)
		goto out;
	if (!invalid && p->kat &&
	    memcmp(cipher + p->aad, p->kat->output, outlen - p->aad)) {
		pr_err("software reference disagrees with known answer\n");
		err = -EBADMSG;
		goto out;
	}
	err = cipher_op(t, p, false, false, plain, cipher, invalid, NULL, iv_out);
	if (err)
		goto out;
	/* RFC3686 and AEAD do not promise to update the caller's IV. */
	if (!invalid && p->a->kind == SKCIPHER && !p->a->rfc3686 &&
	    memcmp(iv_ref, iv_out, p->a->ivsize)) {
		pr_err("encrypt IV mismatch\n");
		err = -EBADMSG;
		goto out;
	}
	if (invalid) {
		err = payload_fill(p, cipher, outlen, true);
		if (err)
			goto out;
	}
	err = cipher_op(t, &ref, true, true, cipher, plain, invalid, NULL, iv_ref);
	if (!err)
		err = cipher_op(t, p, false, true, cipher, plain, invalid, NULL, iv_out);
	if (err || invalid)
		goto out;
	if (p->a->kind == SKCIPHER && !p->a->rfc3686 &&
	    memcmp(iv_ref, iv_out, p->a->ivsize)) {
		pr_err("decrypt IV mismatch\n");
		err = -EBADMSG;
		goto out;
	}
	if (p->a->kind == AEAD) {
		/* Reject separately corrupted tag, ciphertext and AAD. */
		for (i = 0; i < 3; i++) {
			unsigned int pos;

			if ((i == 1 && !p->len) || (i == 2 && !p->aad))
				continue;
			pos = i == 0 ? outlen - 1 : i == 1 ? p->aad : 0;
			cipher[pos] ^= 0x80;
			err = cipher_op(t, p, false, true, cipher, NULL, -EBADMSG, NULL, NULL);
			cipher[pos] ^= 0x80;
			if (err)
				break;
		}
	}
out:
	if (buffer_check(&key) || memcmp(key.data, key_copy, key.len)) {
		pr_err("key buffer modified\n");
		err = -EOVERFLOW;
	}
	buffer_free(&key);
	kfree_sensitive(plain);
	kfree_sensitive(cipher);
	return err;
}

static void hash_set_input(struct ahash_request *req, struct scatter_buffer *src,
			   struct scatterlist tmp[2], unsigned int offset,
			   unsigned int len, u8 *digest)
{
	struct scatterlist *sg = len ? scatterwalk_ffwd(tmp, src->table.sgl, offset) : NULL;

	ahash_request_set_crypt(req, sg, digest, len);
}

static int hash_target(struct transforms *t, const struct params *p,
		       struct scatter_buffer *src, struct buffer *digest)
{
	DECLARE_CRYPTO_WAIT(wait);
	struct ahash_request *req, *imported = NULL;
	struct buffer state = {};
	struct scatterlist tmp[2];
	unsigned int offset = 0, n, step = 0;
	static const unsigned int chunks[] = { 1, 63, 65, 127, 4096 };
	int err;

	req = ahash_request_alloc(t->hash, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	ahash_request_set_callback(req, req_flags(p), crypto_req_done, &wait);
	hash_set_input(req, src, tmp, 0, p->len, digest->data);
	if (!p->stream) {
		err = wait_req(crypto_ahash_digest(req), &wait);
		goto out;
	}
	err = wait_req(crypto_ahash_init(req), &wait);
	if (err)
		goto out;
	/* A zero-byte update must leave the stream and result intact. */
	hash_set_input(req, src, tmp, 0, 0, digest->data);
	err = wait_req(crypto_ahash_update(req), &wait);
	if (err)
		goto out;
	while (offset < p->len) {
		n = min(p->len - offset, chunks[step++ % ARRAY_SIZE(chunks)]);
		if (p->stream == 2 && offset >= p->len / 2)
			break;
		hash_set_input(req, src, tmp, offset, n, digest->data);
		err = wait_req(crypto_ahash_update(req), &wait);
		if (err)
			goto out;
		offset += n;
		if (memchr_inv(digest->data, POISON, digest->len)) {
			pr_err("hash update modified the result buffer\n");
			err = -EOVERFLOW;
			goto out;
		}
		if (p->stream == 3 && !imported && offset >= min(p->len, 65U)) {
			err = buffer_alloc(&state, crypto_ahash_statesize(t->hash), 0);
			if (err)
				goto out;
			err = crypto_ahash_export(req, state.data);
			if (err || buffer_check(&state)) {
				err = err ?: -EOVERFLOW;
				goto out;
			}
			imported = ahash_request_alloc(t->hash, GFP_KERNEL);
			if (!imported) {
				err = -ENOMEM;
				goto out;
			}
			ahash_request_set_callback(imported, req_flags(p), crypto_req_done, &wait);
			err = crypto_ahash_import(imported, state.data);
			if (err || buffer_check(&state)) {
				err = err ?: -EOVERFLOW;
				goto out;
			}
			ahash_request_free(req);
			req = imported;
		}
	}
	hash_set_input(req, src, tmp, offset, p->len - offset, digest->data);
	err = wait_req(p->stream == 2 ? crypto_ahash_finup(req) :
					crypto_ahash_final(req), &wait);
out:
	/* imported aliases req after a successful import. */
	if (imported && imported != req)
		ahash_request_free(imported);
	ahash_request_free(req);
	buffer_free(&state);
	return err;
}

static int hash_case(struct transforms *t, const struct params *p)
{
	struct scatter_buffer src = {};
	struct buffer key = {}, digest = {};
	struct shash_desc *desc = NULL;
	u8 expected[32], key_copy[MAX_KEY];
	u8 *input = NULL;
	int err;

	err = buffer_alloc(&key, p->keylen, p->layout->key_off);
	if (err)
		return err;
	make_key(p, key.data);
	memcpy(key_copy, key.data, key.len);
	input = kmalloc(max(1U, p->len), GFP_KERNEL);
	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(t->hash_ref), GFP_KERNEL);
	if (!input || !desc) {
		err = -ENOMEM;
		goto out;
	}
	desc->tfm = t->hash_ref;
	if (p->kat) {
		memcpy(input, p->kat->input, p->len);
	} else {
		err = payload_fill(p, input, p->len, false);
		if (err)
			goto out;
	}
	if (p->a->hmac) {
		err = crypto_shash_setkey(t->hash_ref, key.data, key.len);
		if (!err)
			err = crypto_ahash_setkey(t->hash, key.data, key.len);
		if (err)
			goto out;
	}
	err = crypto_shash_digest(desc, input, p->len, expected);
	if (err)
		goto out;
	if (p->kat && memcmp(expected, p->kat->output, p->a->digest)) {
		pr_err("software reference disagrees with known answer\n");
		err = -EBADMSG;
		goto out;
	}
	err = scatter_alloc(&src, p->len, p->layout->src_shape, p->layout->src_off, false);
	if (err)
		goto out;
	sg_copy_from_buffer(src.table.sgl, src.nents, input, p->len);
	err = buffer_alloc(&digest, p->a->digest, p->layout->dst_off);
	if (err)
		goto out;
	err = hash_target(t, p, &src, &digest);
	if (buffer_check(&digest) || scatter_check(&src) ||
	    scatter_compare(&src, input, 0, p->len, "hash input", UINT_MAX))
		err = -EOVERFLOW;
	if (!err && memcmp(digest.data, expected, p->a->digest)) {
		pr_err("digest mismatch got=%*phN expected=%*phN\n",
		       p->a->digest, digest.data, p->a->digest, expected);
		err = -EBADMSG;
	}
out:
	if (buffer_check(&key) || memcmp(key.data, key_copy, key.len))
		err = -EOVERFLOW;
	buffer_free(&digest);
	buffer_free(&key);
	scatter_free(&src);
	kfree_sensitive(desc);
	kfree_sensitive(input);
	return err;
}

static bool run_case(struct transforms *t, const struct params *p)
{
	struct params replay = *p;
	struct layout layout = *p->layout;
	unsigned int trial;
	int err;

	if (src_offset >= 0 || dst_offset >= 0 || pad_src || pad_dst) {
		if (src_offset >= 0)
			layout.src_off = src_offset;
		if (dst_offset >= 0)
			layout.dst_off = dst_offset;
		replay.layout = &layout;
		replay.pad_src = pad_src;
		replay.pad_dst = pad_dst;
		p = &replay;
	}
	if (aad_length >= 0) {
		replay.aad = aad_length;
		p = &replay;
	}
	for (trial = 1; trial <= repeats; trial++) {
		if (verbose)
			describe(p, "START", 0, trial);
		err = p->a->kind == HASH ? hash_case(t, p) : cipher_case(t, p);
		tests++;
		if (err) {
			failures++;
			describe(p, "FAIL", err, trial);
		}
		cond_resched();
		if (err && (stop_on_failure || err == -ENOMEM || err == -ETIMEDOUT))
			return true;
	}
	return false;
}

static bool tag_sizes(struct transforms *t, const struct algorithm *a)
{
	unsigned int n, before;
	int err, expected;

	for (n = 0; n <= a->digest + 1; n++) {
		before = crypto_aead_authsize(t->ae);
		err = crypto_aead_setauthsize(t->ae, n);
		expected = (!n || n > a->digest || (!software && n % 4)) ? -EINVAL : 0;
		if (a->gcm)
			expected = crypto_gcm_check_authsize(n);
		tests++;
		if (err != expected ||
		    (err && crypto_aead_authsize(t->ae) != before)) {
			pr_err("FAIL alg=%s setauthsize=%u returned=%d expected=%d before=%u after=%u\n",
			       a->name, n, err, expected, before, crypto_aead_authsize(t->ae));
			failures++;
			if (stop_on_failure)
				return true;
		}
	}
	return false;
}

static void run_algorithm(struct transforms *t, const struct algorithm *a)
{
	struct params p = { .a = a };
	unsigned int k, s, l, i, id = 0;
	unsigned int nkeys = a->kind == HASH ?
		(a->hmac ? ARRAY_SIZE(hmac_keys) : 1) : a->nkeys;

	if (case_id < 0) {
		if (a->kind == AEAD && tag_sizes(t, a))
			return;
		for (i = 0; i < ARRAY_SIZE(known_answers); i++) {
			const struct known_answer *v = &known_answers[i];

			if (strcmp(a->name, v->name))
				continue;
			p.kat = v;
			p.id = i;
			p.len = v->len;
			p.keylen = v->keylen;
			p.authkeylen = v->authkeylen;
			p.aad = v->aadlen;
			p.tag = v->taglen;
			for (l = 0; l < ARRAY_SIZE(layouts); l++) {
				p.layout = &layouts[l];
				p.stream = l % 4;
				if (run_case(t, &p))
					return;
			}
		}
	}
	p.kat = NULL;
	for (k = 0; k < nkeys; k++) {
		p.keylen = a->kind == HASH ? (a->hmac ? hmac_keys[k] : 0) : a->keys[k];
		for (s = 0; s < ARRAY_SIZE(sizes); s++) {
			p.len = sizes[s];
			for (l = 0; l < ARRAY_SIZE(layouts); l++, id++) {
				if ((!num_lengths && p.len > max_size) ||
				    (case_id >= 0 && case_id != id))
					continue;
				p.id = id;
				p.layout = &layouts[l];
				p.iv_pattern = (s + l) % 4;
				p.stream = (s + l) % 4;
				p.aad = a->kind == AEAD ?
					aad_sizes[(s + l) % ARRAY_SIZE(aad_sizes)] : 0;
				p.authkeylen = a->kind == AEAD && !a->gcm ?
					hmac_keys[(k + s + l) % ARRAY_SIZE(hmac_keys)] : 0;
				p.tag = a->kind == AEAD ? 4 * (1 + (s + l) % (a->digest / 4)) : 0;
				if (a->gcm)
					p.tag = gcm_tags[(s + l) % ARRAY_SIZE(gcm_tags)];
				for (i = 0; i < (num_lengths ?: 1); i++) {
					if (num_lengths) {
						p.len = lengths[i];
						pr_info("length override: alg=%s case=%u len=%u\n",
							a->name, p.id, p.len);
					}
					if (run_case(t, &p))
						return;
				}
			}
		}
	}
}

static int __init eip93_test_init(void)
{
	static const char * const groups[] = { "skcipher", "hash", "aead" };
	unsigned int i, selected = 0;
	bool all = !strcmp(group, "all");
	int err;

	if (max_size > 65536 || case_id < -1 || !timeout_ms || !repeats || repeats > 10000 ||
	    aad_length < -1 || aad_length > 1024 ||
	    src_offset < -1 || src_offset >= ALIGNMENT ||
	    dst_offset < -1 || dst_offset >= ALIGNMENT ||
	    (num_lengths && case_id < 0) || (software_async && !software) ||
	    (!all && strcmp(group, groups[0]) && strcmp(group, groups[1]) &&
	     strcmp(group, groups[2])))
		return -EINVAL;
	if (aad_length >= 0) {
		bool aead = false;

		if (case_id < 0 || !alg)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(algorithms); i++)
			if (!strcmp(alg, algorithms[i].name) && algorithms[i].kind == AEAD)
				aead = true;
		if (!aead)
			return -EINVAL;
	}
	if (src_offset >= 0 || dst_offset >= 0 || pad_src || pad_dst) {
		const struct layout *l;
		bool cipher = false;

		if (case_id < 0 || !alg)
			return -EINVAL;
		l = &layouts[case_id % ARRAY_SIZE(layouts)];
		if (l->inplace || l->src_shape != FLAT || l->dst_shape != FLAT)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(algorithms); i++)
			if (!strcmp(alg, algorithms[i].name) && algorithms[i].kind != HASH)
				cipher = true;
		if (!cipher)
			return -EINVAL;
	}
	for (i = 0; i < num_lengths; i++)
		if (lengths[i] > max_size)
			return -EINVAL;
	if (!seed)
		seed = get_random_u32() ?: 1;
	pr_info("begin %s seed=%u max_size=%u case_id=%d lengths=%u repeats=%u src_offset=%d dst_offset=%d pad_src=%u pad_dst=%u aad_length=%d\n",
		software ? "SOFTWARE HARNESS VALIDATION (no EIP93 tested)" : "EIP93 tests",
		seed, max_size, case_id, num_lengths, repeats, src_offset, dst_offset,
		pad_src, pad_dst, aad_length);
	err = payload_init();
	if (err) {
		pr_err("FAIL payload generator setup: %d\n", err);
		return err;
	}
	for (i = 0; i < ARRAY_SIZE(algorithms); i++) {
		const struct algorithm *a = &algorithms[i];
		struct transforms t = {};
		unsigned long oldtests = tests, oldfailures = failures;

		if ((alg && strcmp(alg, a->name)) || (!all && strcmp(group, groups[a->kind])))
			continue;
		selected++;
		err = transforms_alloc(&t, a);
		if (err == -ENOENT || err == -ENODEV) {
			pr_warn("SKIP alg=%s driver unavailable: %d\n", a->name, err);
			skipped++;
		} else if (err) {
			pr_err("FAIL alg=%s allocation/setup: %d\n", a->name, err);
			failures++;
		} else {
			run_algorithm(&t, a);
			pr_info("alg=%s tests=%lu failures=%lu\n", a->name,
				tests - oldtests, failures - oldfailures);
		}
		transforms_free(&t);
		if (failures && stop_on_failure)
			break;
	}
	pr_info("%s selected=%u tests=%lu failures=%lu skipped=%lu async_ops=%lu\n",
		failures || !tests ? "FAIL" : skipped ? "PASS-PARTIAL" : "PASS",
		selected, tests, failures, skipped, async_ops);
	crypto_free_sync_skcipher(payload_cipher);
	return failures ? -EIO : !tests ? -ENODEV : 0;
}

static void __exit eip93_test_exit(void)
{
}

module_init(eip93_test_init);
module_exit(eip93_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EIP93 Crypto API known-answer, SG and streaming correctness tests");
