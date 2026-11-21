// SPDX-License-Identifier: GPL-2.0-only
/* Direct EIP93 NULL-copy / AES-CTR correctness and throughput measurements. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/aes.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/wait.h>

#include "eip93_hw.h"

#define HW_RING_SIZE 512
#define HW_MAX_DEPTH 128
#define HW_MAX_SIZE 65536
#define HW_POISON 0xa5

static char *mode = "both";
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "null, ctr or both");
static char *direction = "both";
module_param(direction, charp, 0444);
MODULE_PARM_DESC(direction, "CTR direction: encrypt, decrypt or both");
static unsigned int key_bits[] = { 128, 192, 256 };
static unsigned int num_keys = ARRAY_SIZE(key_bits);
module_param_array(key_bits, uint, &num_keys, 0444);
MODULE_PARM_DESC(key_bits, "AES key sizes to sweep: 128,192,256 (default all three)");
static unsigned int lengths[32] = { 16, 64, 256, 1024, 4096, 16384, 16400, 65536 };
static unsigned int num_lengths = 8;
module_param_array(lengths, uint, &num_lengths, 0444);
MODULE_PARM_DESC(lengths, "Exact payload lengths in bytes: 1..65536");
static unsigned int src_offset;
module_param(src_offset, uint, 0444);
MODULE_PARM_DESC(src_offset, "Source byte offset from an aligned start: 0..PAGE_SIZE-1");
static unsigned int dst_offset;
module_param(dst_offset, uint, 0444);
MODULE_PARM_DESC(dst_offset, "Destination byte offset from an aligned start: 0..PAGE_SIZE-1");
static unsigned int depths[16] = { 1, 32 };
static unsigned int num_depths = 2;
module_param_array(depths, uint, &num_depths, 0444);
MODULE_PARM_DESC(depths, "Maximum outstanding descriptors: 1..128");
static unsigned int result_batch = 1;
module_param(result_batch, uint, 0444);
MODULE_PARM_DESC(result_batch, "Results per interrupt: 1..128; 1 disables batching");
static unsigned int result_timeout = 5;
module_param(result_timeout, uint, 0444);
MODULE_PARM_DESC(result_timeout, "RDTO exponent N: 0..15, timeout 2^(N+10) clocks when batching");
static bool irq_poll;
module_param(irq_poll, bool, 0444);
MODULE_PARM_DESC(irq_poll, "Keep result IRQs masked while polling and refilling; default off");
static unsigned int poll_budget = 64;
module_param(poll_budget, uint, 0444);
MODULE_PARM_DESC(poll_budget, "Maximum completions per polling round: 1..128");
static unsigned int duration_ms = 250;
module_param(duration_ms, uint, 0444);
MODULE_PARM_DESC(duration_ms, "Timed submission window, 1..60000 ms; includes final drain");
static unsigned int warmup_ms = 50;
module_param(warmup_ms, uint, 0444);
MODULE_PARM_DESC(warmup_ms, "Untimed warmup per configuration, 0..60000 ms");
static unsigned int runs = 3;
module_param(runs, uint, 0444);
MODULE_PARM_DESC(runs, "Trials per configuration, 1..20");
static unsigned int seed = 1708171853;
module_param(seed, uint, 0444);
MODULE_PARM_DESC(seed, "Seed for reproducible software-AES payloads, keys and IVs");
static unsigned int timeout_ms = 10000;
module_param(timeout_ms, uint, 0444);
MODULE_PARM_DESC(timeout_ms, "Maximum time without a completion, 1..60000 ms");
static unsigned int dma_maxburst = 6;
module_param(dma_maxburst, uint, 0444);
MODULE_PARM_DESC(dma_maxburst, "MXBRSTSZ field, 0..15 (encoding, not bytes)");
static unsigned int input_threshold;
module_param(input_threshold, uint, 0444);
MODULE_PARM_DESC(input_threshold, "Input threshold field, 0..255; default 0");
static unsigned int output_threshold = 64;
module_param(output_threshold, uint, 0444);
MODULE_PARM_DESC(output_threshold, "Output threshold field, 0..255; default 64");
static bool force_clocks;
module_param(force_clocks, bool, 0444);
MODULE_PARM_DESC(force_clocks, "Force PE/AES clocks on during measurement; default automatic gating");
static bool dma_skip_cpu_sync;
module_param(dma_skip_cpu_sync, bool, 0444);
MODULE_PARM_DESC(dma_skip_cpu_sync, "Skip payload cache sync for coherency testing; default off");
static unsigned int rows;
module_param(rows, uint, 0444);
static unsigned long long requests;
module_param(requests, ullong, 0444);

struct hw_buffer {
	unsigned long allocation;
	unsigned long attrs;
	unsigned int order, size, offset;
	u8 *data;
	dma_addr_t dma;
	enum dma_data_direction dir;
	bool mapped;
};

struct hw_slot {
	struct hw_buffer src, dst;
	u8 *input, *expected;
};

struct hw_test {
	struct device *dev;
	void __iomem *base;
	int irq;
	wait_queue_head_t wait;
	atomic_t events, fatal;
	struct hw_descriptor *cdr, *rdr;
	dma_addr_t cdr_dma, rdr_dma;
	struct hw_sa *sa;
	struct hw_state *state;
	dma_addr_t sa_dma, state_dma;
	unsigned int write, read, alignment;
	u32 submitted, completed;
	u32 saved_dma, saved_threshold, saved_clock, saved_endian, saved_irq_config;
	unsigned int endian_reg;
	bool started;
};

struct hw_totals {
	u64 count, elapsed;
	unsigned int interrupts;
	unsigned int polls, budget_hits, irq_rearms, rearm_ready;
};

struct hw_setting {
	unsigned int reg;
	u32 value;
};

static bool tried_device;
static int probe_status;

static irqreturn_t hw_interrupt(int irq, void *data)
{
	struct hw_test *t = data;
	u32 status = readl(t->base + HW_INT_STATUS);

	if (!(status & (HW_IRQ_RESULT | HW_IRQ_FATAL)))
		return IRQ_NONE;
	writel(HW_IRQ_RESULT, t->base + HW_INT_DISABLE);
	writel(status, t->base + HW_INT_CLEAR);
	if (status & HW_IRQ_FATAL)
		atomic_or(status & HW_IRQ_FATAL, &t->fatal);
	atomic_inc(&t->events);
	wake_up(&t->wait);
	return IRQ_HANDLED;
}

static bool hw_ready(struct hw_test *t)
{
	struct hw_descriptor *d = &t->rdr[t->read];

	return FIELD_GET(HW_CTRL_OWNER, le32_to_cpu(READ_ONCE(d->ctrl))) == HW_PE_READY &&
	       FIELD_GET(HW_LEN_OWNER, le32_to_cpu(READ_ONCE(d->length))) == HW_PE_READY;
}

static bool hw_wakeup(struct hw_test *t, int events)
{
	return (!irq_poll && result_batch == 1 && hw_ready(t)) ||
	       atomic_read(&t->events) != events || atomic_read(&t->fatal);
}

/* Stop DMA, including on timeout/error, before releasing any DMA storage. */
static void hw_stop(struct hw_test *t)
{
	if (!t->started)
		return;
	writel(HW_IRQ_ALL, t->base + HW_INT_DISABLE);
	synchronize_irq(t->irq);
	writel(HW_CLOCK_PE | HW_CLOCK_AES, t->base + HW_CLOCK_CTRL);
	readl(t->base + HW_CLOCK_CTRL);
	writel(HW_CONFIG_RING | HW_CONFIG_RESET, t->base + HW_PE_CONFIG);
	readl(t->base + HW_PE_CONFIG);
	usleep_range(100, 200);
	writel(0, t->base + HW_RING_CONFIG);
	writel(0, t->base + HW_CDR_BASE);
	writel(0, t->base + HW_RDR_BASE);
	writel(HW_IRQ_ALL, t->base + HW_INT_CLEAR);
	/* Keep reset asserted until hw_restore() selects an idle, non-ring mode. */
	t->started = false;
}

static int hw_program(struct hw_test *t, const struct hw_setting *settings,
		      unsigned int count, const char *stage)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < count; i++)
		writel(settings[i].value, t->base + settings[i].reg);
	for (i = 0; i < count; i++) {
		u32 actual = readl(t->base + settings[i].reg);

		if (actual != settings[i].value) {
			dev_err(t->dev, "%s readback mismatch: reg=%#x requested=%#010x actual=%#010x\n",
				stage, settings[i].reg, settings[i].value, actual);
			err = -EIO;
		}
	}
	return err;
}

static int hw_start(struct hw_test *t)
{
	u32 dma = (t->saved_dma & ~HW_DMA_MAXBURST) |
		  FIELD_PREP(HW_DMA_MAXBURST, dma_maxburst);
	u32 threshold = FIELD_PREP(HW_THRESH_INPUT, input_threshold) |
			FIELD_PREP(HW_THRESH_OUTPUT, output_threshold);
	u32 ring_threshold = FIELD_PREP(HW_RING_RDR_THRESH, result_batch - 1) |
		(result_batch > 1 ? HW_RING_TIMEOUT_EN |
		 FIELD_PREP(HW_RING_RD_TIMEOUT, result_timeout) : 0);
	const struct hw_setting settings[] = {
		{ HW_CLOCK_CTRL, HW_CLOCK_PE | HW_CLOCK_AES },
		{ HW_PE_CONFIG, HW_CONFIG_RING },
		{ t->endian_reg, 0x00e400e4 },
		{ HW_DMA_CONFIG, dma },
		{ HW_BUF_THRESH, threshold },
		{ HW_CDR_BASE, lower_32_bits(t->cdr_dma) },
		{ HW_RDR_BASE, lower_32_bits(t->rdr_dma) },
		{ HW_RING_CONFIG, HW_RING_SIZE - 1 },
		/* RD count must exceed the threshold; timeout drains partial batches. */
		{ HW_RING_THRESH, ring_threshold },
		{ HW_INT_CONFIG, 0 },
	};
	const struct hw_setting clock = {
		HW_CLOCK_CTRL, force_clocks ? HW_CLOCK_PE | HW_CLOCK_AES : 0,
	};
	int err;

	t->started = true;
	writel(HW_IRQ_ALL, t->base + HW_INT_DISABLE);
	writel(HW_CLOCK_PE | HW_CLOCK_AES, t->base + HW_CLOCK_CTRL);
	readl(t->base + HW_CLOCK_CTRL);
	writel(HW_CONFIG_RING | HW_CONFIG_RESET, t->base + HW_PE_CONFIG);
	readl(t->base + HW_PE_CONFIG);
	usleep_range(100, 200);
	/* Release reset before programming registers; no commands are posted. */
	writel(HW_CONFIG_RING, t->base + HW_PE_CONFIG);
	readl(t->base + HW_PE_CONFIG);
	err = hw_program(t, settings, ARRAY_SIZE(settings), "Setup");
	if (err)
		return err;
	writel(HW_IRQ_ALL, t->base + HW_INT_CLEAR);
	writel(HW_IRQ_FATAL, t->base + HW_INT_ENABLE);
	/* Register access needs clocks even when autonomous DMA can wake them. */
	err = hw_program(t, &clock, 1, "Measurement clock");
	if (err)
		return err;
	/* Report the settings verified above, before their clocks could be gated. */
	dev_info(t->dev, "DMA=%#010x thresholds=%#010x endian@%#x=%#010x clocks=%#x ring=%u alignment=%u\n",
		 dma, threshold, t->endian_reg, 0x00e400e4, clock.value,
		 HW_RING_SIZE, t->alignment);
	dev_info(t->dev, "Result interrupts: batch=%u timeout=%s N=%u ring_thresh=%#010x\n",
		 result_batch, result_batch > 1 ? "enabled" : "disabled",
		 result_timeout, ring_threshold);
	dev_info(t->dev, "Completion handling: irq_poll=%u poll_budget=%u\n",
		 irq_poll, poll_budget);
	return 0;
}

static int hw_restore(struct hw_test *t)
{
	const struct hw_setting settings[] = {
		{ HW_CLOCK_CTRL, HW_CLOCK_PE | HW_CLOCK_AES },
		{ HW_PE_CONFIG, 0 },
		{ HW_RING_CONFIG, 0 },
		{ HW_CDR_BASE, 0 },
		{ HW_RDR_BASE, 0 },
		{ HW_DMA_CONFIG, t->saved_dma },
		{ HW_BUF_THRESH, t->saved_threshold },
		{ t->endian_reg, t->saved_endian },
		{ HW_INT_CONFIG, t->saved_irq_config },
	};
	const struct hw_setting clock = { HW_CLOCK_CTRL, t->saved_clock };
	u32 commands, results;
	int err, clock_err;

	/* Reset has cleared command ownership and counts. Release both resets in
	 * idle direct-host mode; do not require RSTRING to read back as asserted.
	 * Never restore the old driver's ring addresses or autonomous mode.
	 */
	writel(HW_CLOCK_PE | HW_CLOCK_AES, t->base + HW_CLOCK_CTRL);
	readl(t->base + HW_CLOCK_CTRL);
	writel(0, t->base + HW_PE_CONFIG);
	readl(t->base + HW_PE_CONFIG);
	err = hw_program(t, settings, ARRAY_SIZE(settings), "Restore");
	commands = readl(t->base + HW_CD_COUNT);
	results = readl(t->base + HW_RD_COUNT);
	if (commands || results) {
		dev_err(t->dev, "Restore rings not empty: commands=%u results=%u\n",
			commands, results);
		err = -EIO;
	}
	/* The saved clock setting may make the registers above unreadable. */
	clock_err = hw_program(t, &clock, 1, "Restore clock");
	return err ?: clock_err;
}

static int hw_buffer_alloc(struct hw_test *t, struct hw_buffer *b,
			   unsigned int len, unsigned int offset,
			   enum dma_data_direction dir)
{
	b->offset = t->alignment + offset;
	b->size = ALIGN(b->offset + len, t->alignment) + t->alignment;
	b->order = get_order(b->size);
	b->allocation = __get_free_pages(GFP_KERNEL | __GFP_ZERO, b->order);
	if (!b->allocation)
		return -ENOMEM;
	b->data = (u8 *)b->allocation + b->offset;
	b->dir = dir;
	b->attrs = dma_skip_cpu_sync ? DMA_ATTR_SKIP_CPU_SYNC : 0;
	memset((void *)b->allocation, HW_POISON, b->size);
	return 0;
}

static int hw_buffer_map(struct hw_test *t, struct hw_buffer *b)
{
	/* Keep cache maintenance aligned, independently of the descriptor address. */
	b->dma = dma_map_single_attrs(t->dev, (void *)b->allocation, b->size,
				      b->dir, b->attrs);
	if (dma_mapping_error(t->dev, b->dma))
		return -EIO;
	b->mapped = true;
	return IS_ALIGNED(b->dma, t->alignment) ? 0 : -EINVAL;
}

static void hw_buffer_free(struct hw_test *t, struct hw_buffer *b)
{
	if (b->mapped)
		dma_unmap_single_attrs(t->dev, b->dma, b->size, b->dir, b->attrs);
	if (b->allocation)
		free_pages(b->allocation, b->order);
}

static int hw_buffer_verify(struct hw_test *t, struct hw_buffer *b,
			    const char *name, const u8 *expected, unsigned int len)
{
	unsigned int first, last, different, offset, count, i;
	bool prefix_bad, suffix_bad;
	int err = 0;

	if (!(b->attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_single_for_cpu(t->dev, b->dma, b->size, b->dir);
	if (memcmp(b->data, expected, len)) {
		err = -EBADMSG;
		first = len;
		last = 0;
		different = 0;
		for (i = 0; i < len; i++) {
			if (b->data[i] == expected[i])
				continue;
			first = min(first, i);
			last = i;
			different++;
		}
		dev_err(t->dev, "%s payload mismatch: first=%u last=%u different=%u\n",
			name, first, last, different);
		offset = first > 8 ? first - 8 : 0;
		count = min_t(unsigned int, 32, len - offset);
		dev_err(t->dev, "%s payload window: offset=%u expected=%*phN actual=%*phN\n",
			name, offset, count, expected + offset, count, b->data + offset);
	}
	prefix_bad = memchr_inv((void *)b->allocation, HW_POISON, b->offset);
	suffix_bad = memchr_inv(b->data + len, HW_POISON, b->size - b->offset - len);
	if (prefix_bad || suffix_bad)
		err = -EOVERFLOW;
	if (err)
		dev_err(t->dev, "%s guards: prefix_bad=%u suffix_bad=%u before=%8phN after=%8phN\n",
			name, prefix_bad, suffix_bad, b->data - 8, b->data + len);
	if (!(b->attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_single_for_device(t->dev, b->dma, b->size, b->dir);
	return err;
}

static void hw_reference(const struct crypto_aes_ctx *aes, u8 *dst,
			 const u8 *src, unsigned int len, const u8 *iv)
{
	u8 counter[AES_BLOCK_SIZE], stream[AES_BLOCK_SIZE];
	unsigned int offset, j, chunk;

	memcpy(counter, iv, sizeof(counter));
	for (offset = 0; offset < len; offset += AES_BLOCK_SIZE) {
		aes_encrypt(aes, stream, counter);
		chunk = min_t(unsigned int, AES_BLOCK_SIZE, len - offset);
		for (j = 0; j < chunk; j++)
			dst[offset + j] = stream[j] ^ (src ? src[offset + j] : 0);
		/* All tested messages fit without wrapping this 32-bit counter. */
		put_unaligned_be32(get_unaligned_be32(counter + 12) + 1, counter + 12);
	}
	memzero_explicit(counter, sizeof(counter));
	memzero_explicit(stream, sizeof(stream));
}

static int hw_prepare(struct hw_test *t, struct hw_slot *slots, unsigned int len,
		      unsigned int depth, unsigned int bits, bool ctr, bool decrypt)
{
	struct crypto_aes_ctx aes;
	u8 key[AES_MAX_KEY_SIZE], iv[AES_BLOCK_SIZE];
	unsigned int i;
	int err;

	for (i = 0; i < sizeof(key); i++)
		key[i] = (seed >> (8 * (i % 4))) ^ (0x37 + 13 * i);
	err = aes_expandkey(&aes, key, bits / 8);
	if (err)
		return err;
	for (i = 0; i < depth; i++) {
		struct hw_slot *s = &slots[i];
		struct hw_sa *sa = &t->sa[i];
		u32 cmd0 = HW_SA_HASH_NULL | HW_SA_PAD_ZERO;
		u32 cmd1 = HW_SA_COPY_PAYLOAD;

		err = hw_buffer_alloc(t, &s->src, len, src_offset, DMA_TO_DEVICE);
		if (err)
			break;
		/* Preserve the initialized guard bytes as well as device writes. */
		err = hw_buffer_alloc(t, &s->dst, len, dst_offset, DMA_BIDIRECTIONAL);
		if (err)
			break;
		s->input = kvmalloc(len, GFP_KERNEL);
		s->expected = kvmalloc(len, GFP_KERNEL);
		if (!s->input || !s->expected) {
			err = -ENOMEM;
			break;
		}
		put_unaligned_be32(seed, iv);
		put_unaligned_be32(i, iv + 4);
		put_unaligned_be32(0x43545200, iv + 8);
		put_unaligned_be32(1, iv + 12);
		memcpy(t->state[i].iv, iv, sizeof(iv));
		iv[0] ^= 0x80;
		hw_reference(&aes, s->input, NULL, len, iv);
		iv[0] ^= 0x80;
		if (ctr) {
			hw_reference(&aes, s->expected, s->input, len, iv);
			if (decrypt)
				swap(s->input, s->expected);
			cmd0 |= HW_SA_CIPHER_AES | HW_SA_IV_STATE;
			if (decrypt)
				cmd0 |= HW_SA_DECRYPT;
			cmd1 = HW_SA_CTR | FIELD_PREP(HW_SA_KEY_SIZE, bits / 64);
		} else {
			memcpy(s->expected, s->input, len);
			cmd0 |= HW_SA_CIPHER_NULL;
		}
		/* SAVE_IV stays clear: each descriptor reloads this slot's counter. */
		sa->cmd0 = cpu_to_le32(cmd0);
		sa->cmd1 = cpu_to_le32(cmd1);
		memcpy(sa->key, key, bits / 8);
		memcpy(s->src.data, s->input, len);
		err = hw_buffer_map(t, &s->src);
		if (err)
			break;
		err = hw_buffer_map(t, &s->dst);
		if (err)
			break;
	}
	memzero_explicit(&aes, sizeof(aes));
	memzero_explicit(key, sizeof(key));
	return err;
}

static int hw_verify(struct hw_test *t, struct hw_slot *slots,
		     unsigned int len, unsigned int depth)
{
	unsigned int i;
	int err;

	for (i = 0; i < depth; i++) {
		err = hw_buffer_verify(t, &slots[i].src, "src", slots[i].input, len);
		if (!err)
			err = hw_buffer_verify(t, &slots[i].dst, "dst", slots[i].expected, len);
		if (err) {
			dma_addr_t src = slots[i].src.dma + slots[i].src.offset;
			dma_addr_t dst = slots[i].dst.dma + slots[i].dst.offset;

			dev_err(t->dev, "Data/guard mismatch: slot=%u len=%u src_offset=%u dst_offset=%u src=%pad dst=%pad err=%d\n",
				i, len, src_offset, dst_offset, &src, &dst, err);
			return err;
		}
	}
	return 0;
}

static void hw_submit(struct hw_test *t, struct hw_slot *s,
		      unsigned int slot, unsigned int len)
{
	struct hw_descriptor *d = &t->cdr[t->write];

	memset(&t->rdr[t->write], 0, sizeof(*d));
	memset(d, 0, sizeof(*d));
	/* PADCTLSTAT=1 + zero padding disables even NULL's default alignment. */
	d->ctrl = cpu_to_le32(FIELD_PREP(HW_CTRL_PAD, 1) | HW_HOST_READY);
	d->src = cpu_to_le32(lower_32_bits(s->src.dma + s->src.offset));
	d->dst = cpu_to_le32(lower_32_bits(s->dst.dma + s->dst.offset));
	d->sa = cpu_to_le32(lower_32_bits(t->sa_dma + slot * sizeof(*t->sa)));
	d->state = cpu_to_le32(lower_32_bits(t->state_dma + slot * sizeof(*t->state)));
	d->cookie = cpu_to_le32(++t->submitted);
	d->length = cpu_to_le32(FIELD_PREP(HW_LEN_OWNER, HW_HOST_READY) | len);
	t->write = (t->write + 1) % HW_RING_SIZE;
}

static int hw_reap(struct hw_test *t, unsigned int len)
{
	struct hw_descriptor *d = &t->rdr[t->read];
	u32 ctrl, length, cookie;

	/* hw_ready() has observed both ownership fields. */
	dma_rmb();
	ctrl = le32_to_cpu(d->ctrl);
	length = le32_to_cpu(d->length);
	cookie = le32_to_cpu(d->cookie);
	if ((ctrl & (HW_CTRL_ERRORS | HW_CTRL_PAD)) ||
	    FIELD_GET(HW_LEN_BYTES, length) != len || cookie != t->completed + 1) {
		dev_err(t->dev, "Bad result: ctrl=%#x length=%#x cookie=%u expected=%u\n",
			ctrl, length, cookie, t->completed + 1);
		return -EIO;
	}
	t->completed++;
	t->read = (t->read + 1) % HW_RING_SIZE;
	return 0;
}

/* ms=0 submits exactly one request per slot for the untimed preflight. */
static int hw_trial(struct hw_test *t, struct hw_slot *slots, unsigned int len,
		    unsigned int depth, unsigned int ms, struct hw_totals *totals)
{
	u64 start, end;
	unsigned long stalled;
	unsigned int irq_start, pending = 0, next_slot = 0;
	u64 sent = 0, done = 0;
	unsigned int n, budget = irq_poll ? poll_budget : depth;
	bool notified = false;
	int err;

	memset(totals, 0, sizeof(*totals));
	/* Old correct output must not hide a missing DMA write in a later trial. */
	for (n = 0; n < depth; n++) {
		struct hw_buffer *b = &slots[n].dst;

		if (!(b->attrs & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_single_for_cpu(t->dev, b->dma, b->size, b->dir);
		memset(b->data, HW_POISON, len);
		if (!(b->attrs & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_single_for_device(t->dev, b->dma, b->size, b->dir);
	}
	start = ktime_get_ns();
	end = start + (u64)ms * NSEC_PER_MSEC;
	stalled = jiffies + msecs_to_jiffies(timeout_ms);
	irq_start = atomic_read(&t->events);
	for (;;) {
		if (atomic_read(&t->fatal)) {
			dev_err(t->dev, "Fatal interrupt: %#x\n", atomic_read(&t->fatal));
			return -EIO;
		}
		if (signal_pending(current))
			return -ERESTARTSYS;
		if (ms ? ktime_get_ns() < end : sent < depth) {
			n = depth - pending;
			while (pending < depth) {
				hw_submit(t, &slots[next_slot], next_slot, len);
				if (++next_slot == depth)
					next_slot = 0;
				sent++;
				pending++;
			}
			if (n) {
				dma_wmb();
				writel(n, t->base + HW_CD_COUNT);
			}
		}
		if (!pending)
			break;
		n = 0;
		if (irq_poll && notified)
			totals->polls++;
		/* An IRQ starts polling; keep it masked across productive rounds. */
		while (((!irq_poll && result_batch == 1) || notified) &&
		       n < min(pending, budget) && hw_ready(t)) {
			err = hw_reap(t, len);
			if (err)
				return err;
			n++;
		}
		if (n) {
			writel(n, t->base + HW_RD_COUNT);
			/* Clearing before decrement can relatch a level-triggered IRQ. */
			writel(HW_IRQ_RESULT, t->base + HW_INT_CLEAR);
			pending -= n;
			done += n;
			notified = irq_poll;
			if (irq_poll && n == budget)
				totals->budget_hits++;
			stalled = jiffies + msecs_to_jiffies(timeout_ms);
			/* Bound each drain/refill round and yield to other runnable work. */
			cond_resched();
		} else {
			int events = atomic_read(&t->events);
			long left = stalled - jiffies;

			if (left <= 0)
				goto timeout;
			totals->irq_rearms++;
			writel(HW_IRQ_RESULT | HW_IRQ_FATAL, t->base + HW_INT_ENABLE);
			/* Close the empty-poll/rearm race before sleeping. A completion
			 * may be below the count threshold with its IRQ still pending.
			 */
			if (irq_poll && notified && hw_ready(t)) {
				writel(HW_IRQ_RESULT, t->base + HW_INT_DISABLE);
				readl(t->base + HW_INT_STATUS);
				totals->rearm_ready++;
				continue;
			}
			err = wait_event_interruptible_timeout(t->wait,
							       hw_wakeup(t, events), left);
			if (err < 0)
				return err;
			if (!err)
				goto timeout;
			notified = true;
		}
	}
	totals->elapsed = ktime_get_ns() - start;
	totals->count = done;
	totals->interrupts = atomic_read(&t->events) - irq_start;
	return done ? 0 : -ENODATA;
timeout:
	dev_err(t->dev, "Completion timeout: pending=%u submitted=%u completed=%u ctrl=%#x length=%#x rd_count=%u\n",
		pending, t->submitted, t->completed,
		le32_to_cpu(READ_ONCE(t->rdr[t->read].ctrl)),
		le32_to_cpu(READ_ONCE(t->rdr[t->read].length)),
		readl(t->base + HW_RD_COUNT));
	return -ETIMEDOUT;
}

static int hw_case(struct hw_test *t, unsigned int len, unsigned int depth,
		   unsigned int bits, bool ctr, bool decrypt)
{
	struct hw_slot *slots;
	struct hw_totals totals;
	unsigned int run, i;
	int err;

	slots = kcalloc(depth, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return -ENOMEM;
	memset(t->sa, 0, HW_MAX_DEPTH * sizeof(*t->sa));
	memset(t->state, 0, HW_MAX_DEPTH * sizeof(*t->state));
	err = hw_prepare(t, slots, len, depth, bits, ctr, decrypt);
	if (err)
		goto out;
	err = hw_trial(t, slots, len, depth, 0, &totals);
	if (!err)
		err = hw_verify(t, slots, len, depth);
	if (err)
		goto out;
	if (warmup_ms) {
		err = hw_trial(t, slots, len, depth, warmup_ms, &totals);
		if (!err)
			err = hw_verify(t, slots, len, depth);
		if (err)
			goto out;
	}
	for (run = 1; run <= runs; run++) {
		u64 mbps;

		err = hw_trial(t, slots, len, depth, duration_ms, &totals);
		if (!err)
			err = hw_verify(t, slots, len, depth);
		if (err)
			goto out;
		mbps = div64_u64(totals.count * len * 8000, totals.elapsed);
		pr_info("row,%s,%s,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u\n",
			ctr ? "ctr" : "null", ctr ? (decrypt ? "decrypt" : "encrypt") : "copy",
			ctr ? bits : 0, len, depth, run, totals.count,
			totals.elapsed, mbps, div64_u64(totals.elapsed, totals.count),
			totals.interrupts, totals.polls, totals.budget_hits,
			totals.irq_rearms, totals.rearm_ready, dst_offset, src_offset);
		rows++;
		requests += totals.count;
	}
out:
	if (err) {
		dev_err(t->dev, "Case failed: mode=%s direction=%s key_bits=%u length=%u depth=%u err=%d\n",
			ctr ? "ctr" : "null", ctr ? (decrypt ? "decrypt" : "encrypt") : "copy",
			ctr ? bits : 0, len, depth, err);
		hw_stop(t);
	}
	for (i = 0; i < depth; i++) {
		hw_buffer_free(t, &slots[i].src);
		hw_buffer_free(t, &slots[i].dst);
		kvfree(slots[i].input);
		kvfree(slots[i].expected);
	}
	kfree(slots);
	return err;
}

static int hw_sweep(struct hw_test *t)
{
	unsigned int i, j, k;
	int err;

	pr_info("kind,mode,direction,key_bits,length,depth,run,requests,elapsed_ns,mbps,ns_per_request,interrupts,polls,budget_hits,irq_rearms,rearm_ready,dst_offset,src_offset\n");
	for (i = 0; i < num_lengths; i++) {
		for (j = 0; j < num_depths; j++) {
			if (strcmp(mode, "ctr")) {
				/* NULL has no key; generate its payload with software AES-128. */
				err = hw_case(t, lengths[i], depths[j], 128, false, false);
				if (err)
					return err;
			}
			if (!strcmp(mode, "null"))
				continue;
			for (k = 0; k < num_keys; k++) {
				if (strcmp(direction, "decrypt")) {
					err = hw_case(t, lengths[i], depths[j], key_bits[k],
						      true, false);
					if (err)
						return err;
				}
				if (strcmp(direction, "encrypt")) {
					err = hw_case(t, lengths[i], depths[j], key_bits[k],
						      true, true);
					if (err)
						return err;
				}
			}
		}
	}
	return 0;
}

static unsigned int hw_endian_register(struct device *dev)
{
	/* Older PonWrt trees identify the engine only by its generic compatible. */
	if (of_device_is_compatible(dev->of_node, "airoha,en7581-eip93") ||
	    of_machine_is_compatible("airoha,an7581") ||
	    of_machine_is_compatible("airoha,en7581") ||
	    of_machine_is_compatible("airoha,an7583") ||
	    of_machine_is_compatible("airoha,en7583"))
		return HW_AIROHA_ENDIAN_CONFIG;
	return HW_ENDIAN_CONFIG;
}

static int hw_probe_device(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hw_test *t;
	u32 options, revision;
	unsigned int i;
	int err, restore_err;

	t = devm_kzalloc(dev, sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	t->dev = dev;
	t->alignment = max_t(unsigned int, 128, dma_get_cache_alignment());
	init_waitqueue_head(&t->wait);
	/* Exclusive resource reservation: never share registers with the driver. */
	t->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(t->base))
		return PTR_ERR(t->base);
	t->irq = platform_get_irq(pdev, 0);
	if (t->irq < 0)
		return t->irq;
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (err)
		return err;
	revision = readl(t->base + HW_REVISION);
	options = readl(t->base + HW_OPTION_1);
	if ((revision & 0xff) != 93)
		return -ENODEV;
	if (strcmp(mode, "null")) {
		if (!(options & HW_OPTION_AES))
			return -EOPNOTSUPP;
		for (i = 0; i < num_keys; i++) {
			if (!(options & BIT(13 + (key_bits[i] - 128) / 64))) {
				dev_err(dev, "Requested AES-%u is unavailable\n", key_bits[i]);
				return -EOPNOTSUPP;
			}
		}
	}
	dev_info(dev, "Direct hardware tests: revision=%#010x options=%#010x seed=%u\n",
		 revision, options, seed);
	dev_info(dev, "mode=%s direction=%s duration_ms=%u warmup_ms=%u runs=%u dma_skip_cpu_sync=%u\n",
		 mode, direction, duration_ms, warmup_ms, runs, dma_skip_cpu_sync);
	dev_info(dev, "Payload offsets: src_offset=%u dst_offset=%u alignment=%u\n",
		 src_offset, dst_offset, t->alignment);
	if (strcmp(mode, "null"))
		for (i = 0; i < num_keys; i++)
			dev_info(dev, "Selected CTR key: %u bits\n", key_bits[i]);
	t->cdr = dma_alloc_coherent(dev, HW_RING_SIZE * sizeof(*t->cdr),
				    &t->cdr_dma, GFP_KERNEL);
	t->rdr = dma_alloc_coherent(dev, HW_RING_SIZE * sizeof(*t->rdr),
				    &t->rdr_dma, GFP_KERNEL);
	t->sa = dma_alloc_coherent(dev, HW_MAX_DEPTH * sizeof(*t->sa),
				   &t->sa_dma, GFP_KERNEL);
	t->state = dma_alloc_coherent(dev, HW_MAX_DEPTH * sizeof(*t->state),
				      &t->state_dma, GFP_KERNEL);
	if (!t->cdr || !t->rdr || !t->sa || !t->state) {
		err = -ENOMEM;
		goto free_dma;
	}
	t->endian_reg = hw_endian_register(dev);
	err = request_irq(t->irq, hw_interrupt, 0, "eip93-hw-test", t);
	if (err)
		goto free_dma;
	/* Snapshot tuning with clocks on, including when the prior driver gated them. */
	t->saved_clock = readl(t->base + HW_CLOCK_CTRL);
	writel(HW_CLOCK_PE | HW_CLOCK_AES, t->base + HW_CLOCK_CTRL);
	readl(t->base + HW_CLOCK_CTRL);
	t->saved_dma = readl(t->base + HW_DMA_CONFIG);
	t->saved_threshold = readl(t->base + HW_BUF_THRESH);
	t->saved_endian = readl(t->base + t->endian_reg);
	t->saved_irq_config = readl(t->base + HW_INT_CONFIG);
	err = hw_start(t);
	if (!err)
		err = hw_sweep(t);
	hw_stop(t);
	free_irq(t->irq, t);
	restore_err = hw_restore(t);
	if (!err)
		err = restore_err;
free_dma:
	if (t->state)
		dma_free_coherent(dev, HW_MAX_DEPTH * sizeof(*t->state), t->state, t->state_dma);
	if (t->sa)
		dma_free_coherent(dev, HW_MAX_DEPTH * sizeof(*t->sa), t->sa, t->sa_dma);
	if (t->rdr)
		dma_free_coherent(dev, HW_RING_SIZE * sizeof(*t->rdr), t->rdr, t->rdr_dma);
	if (t->cdr)
		dma_free_coherent(dev, HW_RING_SIZE * sizeof(*t->cdr), t->cdr, t->cdr_dma);
	if (err)
		dev_err(dev, "FAIL err=%d rows=%u requests=%llu\n", err, rows, requests);
	else
		dev_info(dev, "PASS rows=%u requests=%llu; engine stopped\n", rows, requests);
	return err;
}

static int hw_probe(struct platform_device *pdev)
{
	if (tried_device)
		return -EBUSY;
	tried_device = true;
	probe_status = hw_probe_device(pdev);
	return probe_status;
}

static const struct of_device_id hw_match[] = {
	{ .compatible = "inside-secure,safexcel-eip93i" },
	{ .compatible = "inside-secure,safexcel-eip93ie" },
	{ .compatible = "inside-secure,safexcel-eip93is" },
	{ .compatible = "inside-secure,safexcel-eip93ies" },
	{ }
};

/* Deliberately no MODULE_DEVICE_TABLE: this test must not load automatically. */
static struct platform_driver hw_driver = {
	.driver = {
		.name = "eip93-hw-test",
		.of_match_table = hw_match,
	},
};

static int __init hw_init(void)
{
	unsigned int i, j;
	int err;

	BUILD_BUG_ON(sizeof(struct hw_descriptor) != 32);
	BUILD_BUG_ON(sizeof(struct hw_sa) != 128);
	BUILD_BUG_ON(sizeof(struct hw_state) != 64);
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return -EOPNOTSUPP;
	if ((strcmp(mode, "null") && strcmp(mode, "ctr") && strcmp(mode, "both")) ||
	    (strcmp(direction, "encrypt") && strcmp(direction, "decrypt") &&
	     strcmp(direction, "both")) ||
	    !duration_ms || duration_ms > 60000 || warmup_ms > 60000 ||
	    !timeout_ms || timeout_ms > 60000 || !runs || runs > 20 ||
	    dma_maxburst > 15 || input_threshold > 255 || output_threshold > 255 ||
	    !result_batch || result_batch > HW_MAX_DEPTH || result_timeout > 15 ||
	    !poll_budget || poll_budget > HW_MAX_DEPTH ||
	    src_offset >= PAGE_SIZE || dst_offset >= PAGE_SIZE ||
	    !num_lengths || !num_depths || !num_keys)
		return -EINVAL;
	for (i = 0; i < num_keys; i++) {
		if (aes_check_keylen(key_bits[i] / 8) || key_bits[i] % 8)
			return -EINVAL;
		for (j = 0; j < i; j++)
			if (key_bits[j] == key_bits[i])
				return -EINVAL;
	}
	for (i = 0; i < num_lengths; i++)
		if (!lengths[i] || lengths[i] > HW_MAX_SIZE)
			return -EINVAL;
	for (i = 0; i < num_depths; i++)
		if (!depths[i] || depths[i] > HW_MAX_DEPTH)
			return -EINVAL;
	/* Unlike normal registration, fail if no device can be claimed and tested. */
	err = platform_driver_probe(&hw_driver, hw_probe);
	if (err) {
		pr_err("Load failed: %d; an unbound EIP93 DT device is required\n",
		       probe_status ?: err);
		return probe_status ?: err;
	}
	return 0;
}

static void __exit hw_exit(void)
{
	platform_driver_unregister(&hw_driver);
}

module_init(hw_init);
module_exit(hw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Standalone EIP93 NULL-copy and AES-CTR hardware tests");
