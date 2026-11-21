/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal EIP93 interface for standalone tests. Register definitions follow
 * drivers/crypto/inside-secure/eip93/eip93-regs.h (Richard van Schagen and
 * Christian Marangi) and the ADSP-SC58x/ADSP-2158x hardware reference,
 * September 2017, chapter 44. No dependency on the EIP93 driver.
 */
#ifndef EIP93_HW_TEST_H
#define EIP93_HW_TEST_H

#include <linux/bitfield.h>
#include <linux/types.h>

#define HW_CDR_BASE		0x080
#define HW_RDR_BASE		0x084
#define HW_RING_CONFIG		0x088
#define HW_RING_THRESH		0x08c
#define HW_RING_RDR_THRESH	GENMASK(25, 16)
#define HW_RING_RD_TIMEOUT	GENMASK(29, 26)
#define HW_RING_TIMEOUT_EN	BIT(31)
#define HW_CD_COUNT		0x090
#define HW_RD_COUNT		0x094
#define HW_PE_CONFIG		0x100
#define HW_BUF_THRESH		0x10c
#define HW_DMA_CONFIG		0x120
#define HW_ENDIAN_CONFIG		0x1cc
#define HW_AIROHA_ENDIAN_CONFIG	0x1d0
#define HW_CLOCK_CTRL		0x1e8
#define HW_OPTION_1		0x1f4
#define HW_REVISION		0x1fc
#define HW_INT_STATUS		0x204
#define HW_INT_CLEAR		0x204
#define HW_INT_CONFIG		0x20c
#define HW_INT_ENABLE		0x210
#define HW_INT_DISABLE		0x214

#define HW_CONFIG_RING		(BIT(10) | GENMASK(9, 8))
#define HW_CONFIG_PE_RESET	BIT(0)
#define HW_CONFIG_RING_RESET	BIT(1)
#define HW_CONFIG_RESET		(HW_CONFIG_PE_RESET | HW_CONFIG_RING_RESET)
#define HW_CLOCK_PE		BIT(0)
#define HW_CLOCK_AES		BIT(2)
#define HW_OPTION_AES		BIT(2)
#define HW_IRQ_RESULT		BIT(1)
#define HW_IRQ_FATAL		GENMASK(18, 15)
#define HW_IRQ_ALL		(HW_IRQ_FATAL | GENMASK(11, 9) | GENMASK(1, 0))
#define HW_DMA_MAXBURST		GENMASK(3, 0)
#define HW_THRESH_INPUT		GENMASK(7, 0)
#define HW_THRESH_OUTPUT		GENMASK(23, 16)

#define HW_CTRL_OWNER		GENMASK(1, 0)
#define HW_CTRL_ERRORS		GENMASK(23, 16)
#define HW_CTRL_PAD		GENMASK(31, 24)
#define HW_LEN_OWNER		GENMASK(23, 22)
#define HW_LEN_BYTES		GENMASK(19, 0)
#define HW_HOST_READY		1
#define HW_PE_READY		2

#define HW_SA_IV_STATE		FIELD_PREP(GENMASK(25, 24), 2)
#define HW_SA_HASH_NULL		FIELD_PREP(GENMASK(15, 12), 15)
#define HW_SA_CIPHER_AES		FIELD_PREP(GENMASK(11, 8), 3)
#define HW_SA_CIPHER_NULL		FIELD_PREP(GENMASK(11, 8), 15)
#define HW_SA_PAD_ZERO		FIELD_PREP(GENMASK(7, 6), 3)
#define HW_SA_DECRYPT		BIT(3)
#define HW_SA_KEY_SIZE		GENMASK(26, 24)
#define HW_SA_CTR		FIELD_PREP(GENMASK(9, 8), 2)
#define HW_SA_COPY_PAYLOAD	BIT(2)

struct hw_descriptor {
	__le32 ctrl, src, dst, sa, state, arc4, cookie, length;
};

struct hw_sa {
	__le32 cmd0, cmd1;
	u8 key[32];
	u8 inner_digest[32], outer_digest[32];
	__le32 spi, sequence[2], sequence_mask[2], nonce;
};

/* The hardware state is 56 bytes; reserve 64 to align each slot's IV. */
struct hw_state {
	u8 iv[16];
	__le32 byte_count[2];
	u8 digest[32], reserved[8];
};

#endif
