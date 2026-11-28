// SPDX-License-Identifier: GPL-2.0-only
/* Fragmented ciphertext and separate, cache-line-aligned plaintext pages. */
#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>

static unsigned int repeats = 1;
module_param(repeats, uint, 0444);
MODULE_PARM_DESC(repeats, "Repeat the fragmented GCM matrix");
static char *target = "gcm-aes-eip93";
module_param(target, charp, 0444);
static unsigned int tests;
module_param(tests, uint, 0444);

#define GUARD 0xa5
#define HEADROOM 128
#define PAYLOAD_PER_PAGE (PAGE_SIZE - 2 * HEADROOM)

struct test_sg {
	struct sg_table table;
	unsigned int allocated;
};

static void free_sg(struct test_sg *buf)
{
	struct scatterlist *sg;
	unsigned int i;

	for_each_sg(buf->table.sgl, sg, buf->allocated, i)
		__free_page(sg_page(sg));
	sg_free_table(&buf->table);
}

static int alloc_sg(struct test_sg *buf, const u8 *data, unsigned int length,
		    unsigned int aad, unsigned int fragment, unsigned int offset)
{
	struct scatterlist *sg;
	unsigned int count = 1 + DIV_ROUND_UP(length - aad, fragment);
	unsigned int i, pos = 0;
	int err;

	err = sg_alloc_table(&buf->table, count, GFP_KERNEL);
	if (err)
		return err;
	for_each_sg(buf->table.sgl, sg, count, i) {
		struct page *page = alloc_page(GFP_KERNEL);
		unsigned int bytes = i ? min(length - pos, fragment) : aad;

		if (!page)
			return -ENOMEM;
		memset(page_address(page), GUARD, PAGE_SIZE);
		sg_set_page(sg, page, bytes, HEADROOM + (i ? offset : 0));
		buf->allocated++;
		if (data)
			memcpy(sg_virt(sg), data + pos, bytes);
		pos += bytes;
	}
	return 0;
}

static int verify_sg(struct test_sg *buf, const u8 *expected, unsigned int length)
{
	struct scatterlist *sg;
	unsigned int i, pos = 0;

	for_each_sg(buf->table.sgl, sg, buf->allocated, i) {
		const u8 *base = page_address(sg_page(sg));
		unsigned int bytes = min(length - pos, sg->length);

		if (memchr_inv(base, GUARD, sg->offset) ||
		    memchr_inv(base + sg->offset + sg->length, GUARD,
			       PAGE_SIZE - sg->offset - sg->length))
			return -EOVERFLOW;
		if (memcmp(sg_virt(sg), expected + pos, bytes) ||
		    memchr_inv(sg_virt(sg) + bytes, GUARD, sg->length - bytes))
			return -EBADMSG;
		pos += bytes;
	}
	return pos == length ? 0 : -EINVAL;
}

static int crypt(struct crypto_aead *tfm, struct scatterlist *src,
		 struct scatterlist *dst, unsigned int len, unsigned int aad,
		 bool decrypt)
{
	struct aead_request *req;
	DECLARE_CRYPTO_WAIT(wait);
	u8 iv[12] = { 0x21, 0x54, 0xa7, 0x9b, 0x34, 0x18, 0x4e, 0xff,
		      0x5d, 0xa9, 0x87, 0x16 };
	int err;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				 CRYPTO_TFM_REQ_MAY_SLEEP, crypto_req_done, &wait);
	aead_request_set_ad(req, aad);
	aead_request_set_crypt(req, src, dst, len, iv);
	err = crypto_wait_req(decrypt ? crypto_aead_decrypt(req) :
				       crypto_aead_encrypt(req), &wait);
	aead_request_free(req);
	return err;
}

static int check_case(struct crypto_aead *hw, struct crypto_aead *sw,
		      unsigned int len, unsigned int aad, unsigned int fragment,
		      unsigned int offset, unsigned int keylen)
{
	unsigned int total = aad + len + 16, i, op;
	struct scatterlist ref;
	u8 key[32], *plain = NULL, *cipher = NULL;
	int err = -ENOMEM;

	plain = kvmalloc(total, GFP_KERNEL);
	/* Software reference uses a contiguous, physically backed SG. */
	cipher = kmalloc(total, GFP_KERNEL);
	if (!plain || !cipher)
		goto out;
	for (i = 0; i < total; i++)
		plain[i] = i * 37 + (i >> 8) * 11 + 0x53;
	for (i = 0; i < sizeof(key); i++)
		key[i] = i * 13 + 0x69;
	err = crypto_aead_setkey(sw, key, keylen);
	if (!err)
		err = crypto_aead_setkey(hw, key, keylen);
	if (err)
		goto out;
	memcpy(cipher, plain, total);
	sg_init_one(&ref, cipher, total);
	/* Generate reproducible pseudorandom plaintext with the sync cipher. */
	err = crypt(sw, &ref, &ref, len, aad, false);
	if (err)
		goto out;
	memcpy(plain + aad, cipher + aad, len);
	memcpy(cipher, plain, total);
	err = crypt(sw, &ref, &ref, len, aad, false);
	if (err)
		goto out;

	for (op = 0; op < 3; op++) {
		struct test_sg src = {}, dst = {};
		unsigned int inlen = aad + len + (op ? 16 : 0);
		unsigned int outlen = aad + len + (op ? 0 : 16);
		const u8 *input = op ? cipher : plain;
		const u8 *expected = op ? plain : cipher;

		/* Third operation must reject a damaged tag before writing output. */
		if (op == 2)
			cipher[total - 1] ^= 1;
		err = alloc_sg(&src, input, inlen, aad, fragment, offset);
		if (err)
			goto free_buffers;
		err = alloc_sg(&dst, NULL, outlen, aad, PAYLOAD_PER_PAGE, 0);
		if (err)
			goto free_buffers;
		memcpy(sg_virt(dst.table.sgl), plain, aad);
		err = crypt(hw, src.table.sgl, dst.table.sgl, inlen - aad, aad, op);
		if (op == 2) {
			if (err == -EBADMSG)
				err = verify_sg(&dst, plain, aad);
			else
				err = err ?: -EPROTO;
		} else if (!err) {
			err = verify_sg(&dst, expected, outlen);
		}
		if (!err)
			err = verify_sg(&src, input, inlen);
free_buffers:
		free_sg(&dst);
		free_sg(&src);
		if (err) {
			pr_err("eip93_gcm_sg_test: FAIL len=%u aad=%u fragment=%u offset=%u key=%u op=%u err=%d\n",
			       len, aad, fragment, offset, keylen * 8, op, err);
			goto out;
		}
		tests++;
	}
out:
	kvfree(plain);
	kfree_sensitive(cipher);
	return err;
}

static int check_layouts(struct crypto_aead *hw, struct crypto_aead *sw,
			 unsigned int len, unsigned int aad, unsigned int key)
{
	static const unsigned int fragments[] = { 17, 1440, 1448, 1460 };
	static const unsigned int offsets[] = { 0, 1, 3 };
	unsigned int f, o;
	int err;

	for (f = 0; f < ARRAY_SIZE(fragments); f++)
		for (o = 0; o < ARRAY_SIZE(offsets); o++) {
			err = check_case(hw, sw, len, aad, fragments[f], offsets[o], key);
			if (err)
				return err;
			cond_resched();
		}
	return 0;
}

static int __init test_init(void)
{
	static const unsigned int lengths[] = { 1, 17, 64, 65, 512, 1440,
					       16384, 16385, 65536 };
	struct crypto_aead *hw, *sw;
	unsigned int r, l, key, aad;
	int err;

	if (!repeats || repeats > 100)
		return -EINVAL;
	hw = crypto_alloc_aead(target, 0, 0);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	sw = crypto_alloc_aead("gcm_base(ctr(aes-generic),ghash-generic)",
			       0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(sw)) {
		err = PTR_ERR(sw);
		goto free_hw;
	}
	err = crypto_aead_setauthsize(hw, 16);
	if (!err)
		err = crypto_aead_setauthsize(sw, 16);
	if (err)
		goto out;
	pr_info("eip93_gcm_sg_test: target=%s fragmented source, aligned separate destination\n",
		crypto_aead_driver_name(hw));
	for (r = 0; r < repeats; r++)
		for (key = 16; key <= 32; key += 8)
			for (aad = 5; aad <= 13; aad += 8)
				for (l = 0; l < ARRAY_SIZE(lengths); l++) {
					err = check_layouts(hw, sw, lengths[l], aad, key);
					if (err)
						goto out;
				}
out:
	pr_info("eip93_gcm_sg_test: %s tests=%u\n", err ? "FAIL" : "PASS", tests);
	crypto_free_aead(sw);
free_hw:
	crypto_free_aead(hw);
	return err;
}

static void __exit test_exit(void)
{
}
module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EIP93 GCM fragmented input and aligned output correctness test");
