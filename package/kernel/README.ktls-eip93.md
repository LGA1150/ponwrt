# Testing kTLS and EIP93 outside the kernel source tree

These packages build replaceable kernel modules from a private copy of the
target's **prepared Linux 6.18 sources**:

| Package | Build target | Module | Conflicts with |
| --- | --- | --- | --- |
| `kmod-ktls` | `package/kernel/ktls` | `tls.ko` | `kmod-tls` |
| `kmod-eip93` | `package/kernel/eip93` | `crypto-hw-eip93.ko` | `kmod-crypto-hw-eip93` |

For direct driver correctness testing, [`kmod-eip93-test`](eip93-test/README.md)
adds `eip93_test.ko`. It exercises all 32 EIP93 cipher, hash/HMAC and authenc
algorithms against software references, including key sizes, IV counter wrap,
unaligned/fragmented SG buffers, hash streaming and long HMAC keys. It is loaded
explicitly and can test either EIP93 package; it does not require kTLS.
Test release 7 extends inverse-block failure diagnostics from ECB to CBC,
including the initial IV or preceding ciphertext used for chaining.

Test release 10 also includes an [aligned CTR throughput sweep](eip93-test/README.ctr-bench.md)
through the Crypto API. For an independent baseline,
[`kmod-eip93-hw-test`](eip93-hw-test/README.md) supplies `eip93_hw_test.ko`,
which measures NULL-copy and AES-CTR directly using registers, DMA rings
and IRQs. It claims the device exclusively with the normal driver unloaded.
This separate package does not copy or depend on the crypto driver's sources.
Both benchmarks accept exact byte lengths, including common TCP MSS sizes.
Standalone hardware test release 6 adds the default-off `dma_skip_cpu_sync`
option to compare payload mappings without cache synchronization.

[`kmod-ghash-bench`](ghash-bench/README.md) measures the other half of generic
GCM through the Crypto API's ahash interface. On ARM64 it selects the
`ghash`, `ghash-generic` and `ghash-neon` implementations and includes a
separate [`ghash-neon-shuffle`](crypto-ghash-neon-shuffle/README.md) module
for explicit comparison with the built-in or modular stock driver. Release 11
of that module exposes `ghash-neon-sched` for instruction-scheduling tests
and `ghash-neon-gpr` for next-block input loads through two general-purpose
registers, plus `ghash-neon-2x` for two-block aggregation with one reduction
and `ghash-neon-2x-pmull` for a byte-PMULL reduction comparison. The separate
`ghash-neon-pmull` tests that reducer without aggregation against scheduled.
`ghash-neon-orig-pmull` changes only the stock loop's reducer, preserving
its multiplication, ZIP/EXT reconstruction and input-load order.
`ghash-neon-fused` precomputes a folded key and combines multiplication
with reduction, using two product sums and one final fold per block.
It passes build/static and device short/SG checks, but runs 7.7--8.1% slower
than original-loop PMULL at 1440 through 65536 bytes in both benchmark orders.
`ghash-neon-2x-reuse` alternates the two-block PMULL product order to retain
one key table, reducing table reads from 240 to 160 bytes for each pair after
the first. It passes build/static and device short/SG checks, gaining
4.8--5.5% over two-block PMULL at 1440 through 65536 bytes in both orders.
`ghash-neon-2x-load` keeps that reuse and overlaps remaining key-table loads
with multiplication using six caller-saved GPRs and 64-bit NEON loads/lane
inserts. It passes ARM64 build, LE/BE static checks and device short/SG/GCM
boundary checks across three seeds. Bulk measurements gain 6.2--6.8% over
`ghash-neon-2x-reuse` in both orders, reaching 1.488306 Gbit/s at 64 KiB.
QEMU TCG passes 720 existing benchmark rows with full crypto self-tests and
KASAN, plus 2,880 direct-shash comparisons through 131,089 bytes to cover
the internal 64 KiB loop limit and exported/imported state. No driver fixes
were needed; QEMU rates are not used as device throughput measurements.
Release 11 adds `ghash-neon-4x`, sharing three product reconstructions and
one reduction per four blocks, with precomputed H^3/H^4 and the existing
`ghash-neon-2x-load` short/tail path. It retains key-load scheduling and
register-only accumulation. Build/static checks, 922 QEMU benchmark rows
and 5,400 direct-shash comparisons through 131,121 bytes pass with full
crypto self-tests and KASAN. The device comparison passes 180 rows and
111,185,351 timed requests, gaining 19.3% at 1440 bytes and about 22.5% at
16384 through 65536 bytes in both orders. At 64 KiB, pooled median throughput
rises from 1.488876 to 1.823721 Gbit/s, making four-block aggregation the
fastest measured bulk variant. This capture uses raw single-entry input and
one seed; reference/final-slot and guard checks pass, with intermediate
digests reused. It does not measure complete GCM or kTLS performance.
Release 12 adds `ghash-neon-8x`, accumulating eight blocks before the same
three product reconstructions and one reduction. It uses three input passes
to avoid assembly spills, precomputes powers through H^8, and uses the
four-block implementation for short calls and tails. Its 1696-byte key
context and extra input/key loads yield no useful bulk gain in the device
comparison: 220 rows and 123,849,377 timed requests pass, but 8x is
3.5--7.6% slower at 128--256 bytes and 0.8--0.9% slower at 1440 bytes in
both orders. The 16--64 KiB medians are within 0.07% of 4x. Keep 4x as
the preferred implementation; the capture has the same verification
limits as the four-block measurements above.
Release 13 adds `ghash-neon-orig-pmull13`, a separate 13-instruction
byte-PMULL reducer for comparison with the original-loop 15-instruction
variant. A masked constant places the correction product directly in the
high two bytes, saving two instructions per block and adding two setup
instructions per call. The little-endian loop has 133 instructions with
the original input loads, products and key format. The A53 comparison
passes and gains about 0.7% at 1440 through 65536 bytes; see the
[comparison commands](crypto-ghash-neon-shuffle/README.md#thirteen-instruction-reducer-experiment-release-13).
Release 14 adds explicitly selected `ghash-neon-pmul` and
`ghash-neon-umull` multiplication experiments. Their complete loops contain
152 and 235 instructions per block versus 133 for `orig-pmull13`.
Both pass the A53 digest/guard checks but lose 17.5% and 62.5% throughput
at 64 KiB relative to `orig-pmull13`. See the
[implementation and tests](crypto-ghash-neon-shuffle/README.md#pmul-and-integer-umull-experiments-release-14).
Release 15 adds `ghash-neon-orig-pmull12`, combining low-byte extraction
and rotation with one two-register `TBL`. Its complete loop has 132
instructions, with three additional setup instructions and one 16-byte
constant load per call. A53 throughput is not yet measured. See the
[comparison commands](crypto-ghash-neon-shuffle/README.md#twelve-instruction-reducer-experiment-release-15).
All previous implementations remain available for comparison.
Benchmark release 2 recognizes an already registered stock driver without
loading `ghash-ce.ko`; that package is optional. It supports MSS and full-record
sizes, SG chunking, request depths, optional GCM-formatted AAD and CPU-time reporting.
An independent GHASH reference checks digests outside timing. It requires
neither kTLS nor EIP93, and leaves the production driver's bouncing behavior
unchanged. See its README for the first comparison and interpretation limits.

The separate userspace [`memcpy-bench`](../utils/memcpy-bench/README.md)
package provides a CPU-copy baseline. It streams libc copies through two
128 MiB buffers by default, supports copy blocks up to 128 MiB, and reports
verified payload throughput, CPU time and page faults. It can also be built
directly from its standalone C source without any kernel test module.

Module compilation uses Kbuild's external-module `M=...` interface. Package
patches affect only the private copy. Changing one of these patches therefore
does not require recompiling the kernel itself, provided its configuration and
ABI stay the same. Both packages retain OpenWrt's exact kernel dependency.

The sources are copied at package preparation time, so stable kernel fixes and
PonWrt target patches are retained without downloading a second kernel tree.
The recipes copy only C files, headers and Makefiles, never existing objects.
They require a prepared kernel tree; an SDK without these source directories
is insufficient.

## Contents

`kmod-ktls` carries the module changes from
[`LGA1150/linux`, `tls-rx-pipelining-v6.18`](https://github.com/LGA1150/linux/tree/0734576ba622/net/tls).
The ordered patch series includes:

1. `054dac15f156`: factor record consumption from stream-parser notification.
2. `c6c754edc034`: suppress spurious receive notifications.
3. `147e967dc3c8`: bound consecutive empty records.
4. `1408ac7aaa8a`: consume empty records on splice receive.
5. `fe75db300d8e`: preserve authenticated data before an async receive failure.
6. `ee7de40a610b`: pipeline TLS 1.3 receive decryption.
7. `97f40f2fff04`: reject exhausted transmit record sequences.
8. `2a1919c089b0`: drain pending transmit records before rekeying.
9. `aa81742f2bb1`: pipeline TLS 1.2 splice receive.

The prerequisite fixes precede the pipeline changes. Only `net/tls/` and
`include/net/tls.h` changes are included; the Linux selftests and standalone
benchmark remain in that Linux branch.

This is **software kTLS with Crypto API acceleration**, for TLS 1.2/1.3
`recv`, `send` and `splice`. NIC TLS offload (`CONFIG_TLS_DEVICE`) and TCP stack
bypass (`CONFIG_TLS_TOE`) are disabled. The patched receive-context header is
private to this module and is placed before the base kernel's include paths.
It must not be used with NIC drivers compiled against the original RX layout.

`kmod-eip93` uses the target's existing EIP93 implementation,
including PonWrt's small-request software fallback patch. Release 27 disables
that size-based AES fallback by default so small requests exercise hardware.
The load-time `aes_sw_fallback=1` option restores the stock thresholds
(AES-192/256: 256 bytes; AES-128: 512 bytes). Release 37 keeps the raw
`dma_maxburst=6` default and 64/64 DMA buffer thresholds. Read-only AES
skcipher sources can be byte-aligned; DES/3DES and all native authenc sources
require word alignment or bounce. The earlier short-authenc-only guard
also allowed a long AES-authenc request to fail on the device.
Release 29 fixes the returned ordinary CTR IV when the low 32-bit counter
wraps immediately after the last block. A separate intermittent AES-CBC
input-tail failure remains under investigation; no CBC source bounce is added.
Release 30 extends the opt-in authenc diagnostics with tag bytes and the
buffer-threshold register; it does not change DMA or alignment policy.
Release 31 adds a default-off `skcipher_partial_src` diagnostic for direct
partial-block CTR reads from a single source SG entry. It is limited to
out-of-place AES skcipher requests and leaves normal behavior unchanged.
Release 32 adds a default-off `skcipher_tail_align` experiment for the same
CTR source layout. It processes up to three extra bytes from existing source
SG padding to align the input end, uses private destination space and copies
back only the logical length. Ordinary CTR's returned IV follows that logical
length. No source copy is added; insufficient source space returns `-EMSGSIZE`.
Release 33 removes the source-padding requirement for that diagnostic. It
keeps the supplied source SG and DMA length unchanged and extends only the
descriptor walk's input budget. The final read can consume up to three bytes
past the recorded DMA span; its end must remain word-aligned after mapping.
A changed DMA word offset returns `-EINVAL` before submission and unwinds
preparation. Private output and logical-length IV advancement are retained.
See the [unpadded-source replay](eip93-test/README.md#ctr-overread-with-unpadded-sources).
Release 34 extends the opt-in tail-alignment experiment to multi-entry
CTR sources with block-aligned intermediate lengths. This includes generic
GCM/CCM's tag prefix plus payload. Each descriptor has its own logical IV
state and a separate padded output slot, so discarded bytes cannot advance
the next descriptor's counter or overwrite its output. The source SG is
mapped unchanged; all preparation precedes submission. See the
[kTLS replay](eip93-test/README.md#multi-entry-ctr-tail-alignment).
Release 35 removes that intermediate-length restriction. It handles bytes
belonging to AES blocks split across source entries with synchronous software
AES/CTR, while hardware processes the remaining spans. Source data is read
from the supplied SG without a DMA bounce or private source SG. Software
processing starts after DMA unmapping and finishes before any output copy,
including when distinct SG tables alias the same pages. This uses the same
default-off option; see the
[partial-entry replay](eip93-test/README.md#ctr-across-partial-sg-entries).
Release 36 places these CTR chunks at their logical output offsets. Later
ordered DMA writes overwrite preceding overread output; software boundary
bytes are filled after DMA completes. Only the trailing overread extent is
reserved, and completion uses one ordinary bounced-output copy. This
removes intermediate gaps and repeated destination SG walks. Source reads,
independent counters and the default-off option are retained. See the
[overlapping-output replay](eip93-test/README.md#contiguous-ctr-output).
Release 37 adds default-off aggregate skcipher profiling and an installed
`eip93-measure-ktls` runner. It separates paths and directions and measures
ID allocation, preparation, DMA mapping, submission and completion without
per-request logging. The runner saves uninstrumented throughput controls
and restores the settings it changes. See the
[profiling command and timing definitions](eip93/README.profile.md).

The subsequent release-37 device report records a plaintext mismatch with
tail alignment off at input threshold `0x40`. At threshold zero, both
20-trial captures pass, with a 7.23% median throughput loss when tail
alignment is enabled. GHASH dominates active kernel sampling. See the
[device perf comparison](eip93/README.profile.md#device-perf-comparison-at-input-threshold-zero).
The subsequent runner capture also passes: avoiding source copies cuts
preparation time from 23.39 to 12.51 us per request, but descriptors rise
from 1.42 to 2.78 and aggregate throughput falls 9.25% in the uninstrumented
controls. Completion/copyback time is unchanged or lower; longer pending
time includes queueing and does not directly measure AES execution. See
the [device stage counters](eip93/README.profile.md#device-stage-counters-at-input-threshold-zero).

Test package release 10 includes a separate `eip93_ctr_bench.ko` module for
aligned CTR length sweeps, including partial final blocks. It uses one
aligned source/destination SG entry per request and configurable queue
depths, with software reference
checks outside the timing windows. See the
[benchmark commands and accounting](eip93-test/README.ctr-bench.md).

Destination/in-place cache-line isolation and
private authenc decryption output remain enforced. See the
[normal-use test](eip93-test/README.md#normal-use-policy) before enabling any
diagnostic bypasses.

Release 5 added async backpressure after the device test confirmed
`Failed to allocate request ID: -28` under the default TX benchmark. The IDR
checks in release 4 prevented the NULL-request panic, but returned `-ENOSPC`
to stock kTLS, which aborted transmission with `EBADMSG`.
Release 7 added cache-line isolation to DMA-written buffers, and the default
TLS 1.2 TX benchmark passed on the device. Release 8 removed the algorithms'
`cra_alignmask` initializers. Release 10 restores release 8's driver sources
after removing the 4-byte SG offset check in release 9 caused a TLS 1.2 RX
plaintext mismatch. The same command passes with release 8 on the device.
Release 11 adds HMAC key normalization, authentication tag validation, and
two fixes to PonWrt's small-request fallback. It retains the 4-byte SG
offset check and does not change the DMA burst configuration.
Release 12 adds a synchronous authenc fallback for zero-payload requests,
including tag-only and AAD-only messages, after the direct tests exposed an
empty DMA mapping in release 11.
Release 13 verifies the tags produced by basic hash-decrypt. Release 12's
first nonempty authenc case accepted a corrupted tag on the device.
Release 14 preserves AAD byte offsets and falls back for offsets outside the
hardware's documented ranges.
Release 15 adds optional diagnostics for the intermittent SHA-256/RFC3686
authenc failure. Release 16 reads authenc tags from the aligned saved hash
state instead of the packet tail, after diagnostics isolated a duplicated
neighboring tag byte. Case 281 then passed on Airoha, but case 323 exposed
similar corruption in a partial word of decrypted payload. Release 17 uses
the synchronous authenc fallback when AAD plus payload is not word-multiple.
Case 323 is intermittent: a later full authenc run on release 16 passed.
Release 18 adds opt-in DMA address logging and independent bounce-buffer
offsets for investigating the physical boundary conditions.
Release 19 adds a probe-time override for the raw `MXBRSTSZ` field while
preserving the existing DMA configuration by default.
Release 20 sets both DMA buffer thresholds to 64 bytes for all EIP93 devices
(`PE_BUF_THRESH=0x00400040`). It then reverts the partial-word fallback and
saved-hash-state tag workaround in separate patches. Authenc again uses
packet-tag DMA, including partial output words; host authentication checks,
private decryption destinations and SG alignment checks remain. The
`aead_allow_partial` parameter is removed because it no longer has a guard
to bypass. `MXBRSTSZ` is still preserved by default.
Release 21 removes the four-byte SG start-offset check after the threshold
change. Read-only sources can use byte-aligned DMA addresses when the
remaining length and SG layout checks permit it. Destinations and in-place
buffers still require complete cache lines or use a bounce buffer.
Release 22 restores that check after release 21 failed device tests at
multiple cases, including the final DES block of ECB case 322. Both DMA
thresholds remain 64 bytes. The prepared driver sources match release 20.
Release 23 adds opt-in skcipher DMA logging and a diagnostic bypass for
read-only source offsets. Both default off, retaining release 22 behavior.
Test release 5 independently controls source/destination offsets and
destination padding, and inverts a bad ECB block with software crypto.
Release 24 adds an independent, opt-in source-offset bypass for authenc
to extend the burst comparison to AEAD. All diagnostic bypasses default
off, and the existing DMA burst field is still preserved at probe.
Test release 6 adds source SG padding and extends the flat, out-of-place
offset controls to AEAD, so request length and mapped extent can be varied
independently. That test-only update left the driver at release 24.
Release 25 defaults the raw DMA maximum burst field to 6. An explicit
`dma_maxburst=-1` still preserves the register value found at probe.
Release 26 limits source word-alignment checks to DES/3DES and authenc
inputs smaller than eight bytes. Eligible AES sources otherwise use direct
DMA without a diagnostic override. The test module remains at release 6.
Release 27 disables the size-based AES fallback from distribution patch 926
by default. It can be restored at module load without rebuilding the kernel.
Release 28 restores word-aligned sources for all native authenc lengths
after long-message case 740 failed with an unaligned source. AES skcipher
sources retain the release 26 policy and the size-based fallback stays off.
Release 29 propagates the final low-word counter carry into the returned
ordinary CTR IV, including requests ending with a partial block. Existing
splits for counter wrap inside a request are retained.
Release 30 logs both tags on an authenc mismatch and records the buffer
thresholds alongside the DMA configuration when mapping diagnostics are on.

The EIP93 patches apply in filename order, with each fix in its own commit:

| Patch | Change |
| --- | --- |
| 001 | Reserve and check cipher request IDs before preparation. |
| 002 | Reserve hash request IDs before submitting intermediate blocks. |
| 003 | Diagnose unmatched completion IDs without dereferencing NULL. |
| 004 | Unwind cipher preparation failures without a second completion or copying uninitialized output. |
| 005 | Keep cipher direction and AAD controls in a private SA for each request. |
| 006 | Map cipher IV state bidirectionally, including CTR wrap state. |
| 007 | Prepare all hash DMA mappings before submission and safely unwind failures. |
| 008 | Copy and acknowledge results before recycling ring slots. |
| **009** | **Queue requests and wait for ring/request-ID credits: the backpressure fix.** |
| **010** | **Bounce DMA-written SG ranges that do not own complete cache lines.** |
| 011 | Remove Crypto API alignment masks; keep the driver's DMA alignment and bounce handling. |
| 013 | Use the full hash digest when shortening a long HMAC key, independently of tag truncation. |
| 014 | Reject authentication tag sizes that the hardware's word-count field cannot represent. |
| 015 | Apply the generic software fallback threshold to AES-192/256. |
| 016 | Free the software fallback if cipher SA allocation fails during initialization. |
| 017 | Handle zero-payload authenc requests with a synchronous fallback, without cipher DMA. |
| 018 | Compare the computed authenc tag before copying decrypted data back. |
| 019 | Preserve AAD byte offsets and fall back for unrepresentable lengths. |
| 020 | Add optional authenc hardware-status and tag-mismatch diagnostics. |
| 021 | Read authenc tags from saved hash state; reverted by 027. |
| 022 | Use synchronous authenc for partial output words; reverted by 026. |
| 023 | Add opt-in authenc DMA address/completion logging and independent bounce offsets. |
| 024 | Add a probe-time raw MXBRSTSZ override and log the actual DMA configuration. |
| 025 | Set both DMA buffer thresholds to 64 bytes for all EIP93 devices. |
| 026 | Restore hardware authenc for partial output words and remove `aead_allow_partial`. |
| 027 | Restore packet-tag DMA and the original IV/state unmapping order. |
| 028 | Remove the four-byte SG start-offset check; reverted by 029. |
| 029 | Restore four-byte SG start-offset validation after device failures at 64/64 thresholds. |
| 030 | Add opt-in skcipher source alignment bypass and DMA/descriptor/completion logging. |
| 031 | Add an independent opt-in authenc source alignment bypass. |
| 032 | Default the raw DMA maximum burst field to 6. |
| 033 | Allow byte-aligned AES sources; retain source bouncing for DES/3DES and short authenc inputs. |
| 034 | Disable size-based AES software fallback by default; allow a load-time opt-in for comparisons. |
| 035 | Require word-aligned sources for all authenc lengths after the case-740 regression. |
| 036 | Propagate a final low-32-bit counter wrap into the returned ordinary CTR IV. |
| 037 | Add tag bytes and the buffer-threshold register to opt-in authenc diagnostics. |
| 038 | Add an opt-in direct-source diagnostic for single-entry partial-block CTR reads. |
| 039 | Add single-entry CTR tail alignment with private output and logical-length IV accounting. |
| 040 | Overread the supplied CTR source SG without changing its mapping; check mapped alignment and unwind failures. |
| 041 | Extend opt-in CTR tail alignment to multi-entry sources with independent logical counters and padded output slots. |
| 042 | Handle AES blocks split across CTR source entries with software boundary processing and direct hardware spans. |
| 043 | Overlap CTR overread output at logical offsets and copy back the contiguous result in one SG walk. |
| 044 | Add opt-in aggregate skcipher stage timings and descriptor/copy counters, separated by path and direction. |

Apply all forty-three patches in filename order through 044. Patch 012
remains withdrawn; patch 028's retry with lower DMA thresholds is reverted
by patch 029.
Patches 004–008 make preparation, completion and
ring reuse safe for deferred
submission; they are separate so these changes can be reviewed and bisected.
The previous IDR-only package remains available at
[revision f600a23](https://github.com/LGA1150/ponwrt/tree/f600a2304d24ad8e8f403c10789a8d107cac9e6f/package/kernel/eip93).

Patches 001–009 are also available as individual commits against Linux
`master` on the
[`eip93-backpressure` review branch](https://github.com/LGA1150/linux/compare/master...eip93-backpressure).

Patch 013 fixes authenc HMAC keys longer than the 64-byte hash block size.
For example, HMAC-SHA256 must use all 32 bytes of the shortened key even
when the requested tag is only 16 bytes. Patch 014 rejects tag sizes that
are not multiples of four instead of silently rounding down the hardware
digest length; the AEAD core already rejects zero and oversized tags.
These two patches affect HMAC/authenc rather than the generic GCM/CCM
constructions used by the TLS benchmark.

Patch 015 fixes an always-true conditional in PonWrt's fallback patch.
When fallback is enabled, AES-192/256 requests longer than 256 bytes
use hardware; AES-128 retains the 512-byte software threshold. Patch
016 releases the already allocated fallback when SA allocation fails.

Patch 034 disables this size-based skcipher fallback by default, independently
of the prepared kernel's `CONFIG_CRYPTO_DEV_EIP93_*_SW_MAX_LEN` values.
All valid nonempty AES ECB/CBC/CTR requests now enter the hardware queue;
RFC3686 already bypassed this fallback. Zero-length skcipher requests remain
no-ops, and malformed block lengths are still rejected. The separate authenc
fallbacks for empty payloads and unsupported AAD offsets remain enabled.

`aes_sw_fallback` is read-only after load because transform allocation and
request sizing depend on it. An explicit `aes_sw_fallback=1` restores the
configured thresholds for comparisons; otherwise no skcipher fallback
transform is allocated or keyed. Rebuilding only `kmod-eip93` is sufficient:
the stock kernel configuration and distribution patch 926 are unchanged.

Patch 036 corrects ordinary CTR completion after the last consumed counter
is `ffffffff`. The engine updates only its low 32 counter bits. A message
that continues past that counter already gets two states and a descriptor
split, but one ending there needs only a carry in the IV returned to the
caller. For example, an all-ones IV and a one-byte message must return an
all-zero IV. This applies to encryption and decryption; RFC3686 and native
authenc retain their existing behavior.

The device's three-repeat matrix exposed 198 CTR IV mismatches, with correct
ciphertext, after the size-based fallback was disabled. Its one separate
AES-CBC failure affected the final block of case 234 (512 bytes). A focused
replay failed at repetition 596 with source offset 1, while offset 4 passed
1,000 repetitions. No source bounce or software fallback is added for CBC.
The direct input path remains available for investigating DMA refill timing;
see the [CBC threshold comparison](eip93-test/README.md#cbc-input-threshold-comparison).

Patch 017 handles authenc messages without a cipher payload. Encrypting an
empty message with no AAD must produce its HMAC tag, but the old path called
`dma_map_sg()` with zero source entries, warned in `__dma_map_sg_attrs()` and
returned `-ENOMEM`. Tag-only decryption has the corresponding empty
destination. A synchronous software authenc transform handles both directions,
including AAD-only messages and corrupted-tag rejection. The driver propagates
keys, request flags and accepted tag sizes to the fallback; its existing
four-byte tag-size restriction still applies. Requests with a nonempty cipher
payload continue through the hardware queue.

Patch 018 fixes missing authentication in the basic hash-decrypt path.
Automatic authentication errors are documented for protocol operations;
the basic operation generates a digest for the host to compare. Decryption
now retains digest output and uses a separate destination with tag space,
including for in-place requests. After DMA unmapping, the driver converts
SHA digest words and compares the received and computed tags in constant
time. A mismatch returns `-EBADMSG` without copying decrypted data back.
Cipher and HMAC computation remain on EIP93. This affects native authenc;
kTLS GCM/CCM uses separate Crypto API authentication.

Patch 019 fixes independent truncation of the AAD length in the hardware's
eight-bit hash/crypt offset. Word-aligned lengths up to 1020 bytes use word
offsets. Unaligned lengths from 4 through 255 bytes use the documented byte
offset mode. Other AAD lengths, including one and three bytes, use the existing
synchronous authenc fallback. Neither mode silently rounds or wraps the cipher
start position. The range limits come from the SHARC+ reference, table 44-69,
for operations loading their IV from SA state.

Patch 020 adds the root-writable `aead_debug` parameter (default `0`). Enable
it with `echo 1 > /sys/module/crypto_hw_eip93/parameters/aead_debug` while
replaying a failing case. A hardware failure prints `authenc hardware error`
with raw result-descriptor control and length words. A host tag mismatch
prints `authenc tag mismatch` with lengths, flags, bounce-buffer use and a
`different` mask: bit 0 represents the first tag byte, bit 31 the last byte
of a SHA-256 tag. A following `authenc verification error` includes raw result
status and length even when the hardware reported success (`hw_err=0`).
This driver diagnostic contains no key, plaintext or tag
values. Intentional corrupted-tag/ciphertext/AAD tests also produce these
messages; correlate them with the test module's final result. Disable logging
afterward with `echo 0 > /sys/module/crypto_hw_eip93/parameters/aead_debug`.

Patches 021 and 022 were workarounds for intermittent authenc DMA tail
corruption: 021 read tags from saved SA hash state, and 022 routed partial
output words to synchronous software. Release 20 reverts both after lowering
the DMA thresholds. The engine appends its digest to the packet again in
both directions. Completion normalizes SHA digest words and verifies the
received tag with `crypto_memneq()` before copying decrypted data back.
The private decryption destination includes space for the computed tag;
only AAD and plaintext are copied to the caller after successful verification.
IV and CTR-wrap state are unmapped in the existing completion cleanup.

Partial-word lengths now reach hardware without a diagnostic override.
Empty-payload and unrepresentable-AAD fallbacks remain, as does cache-line
isolation for DMA-written buffers. Release 22 restores the four-byte SG
start-offset check after device testing showed that the lower thresholds
were insufficient to make byte-aligned source DMA reliable. Release 26
narrows that source check to DES/3DES and short authenc inputs after the
subsequent cipher and length comparisons. Release 28 extends it back to all
native authenc lengths after a long-message failure, while retaining direct
byte-aligned AES skcipher sources.

Patch 023 adds diagnostic parameters under
`/sys/module/crypto_hw_eip93/parameters/`. All default to zero:

- `aead_dma_debug`: log mapped source/destination DMA addresses, mapping and
  processing lengths, bounce use, SG offsets, SA/state addresses, raw DMA
  configuration and every authenc completion. No packet or key bytes are logged.
- `aead_src_offset`, `aead_dst_offset`: independently shift authenc bounce
  buffers by the specified byte count. Values must be multiples of both four
  and `dma_get_cache_alignment()`, smaller than `PAGE_SIZE`. A nonzero value
  forces that buffer to bounce. Allocation and cleanup include the offset and
  cache-line padding; changing a parameter does not change an existing
  allocation's free address or order.

Release 20 removes the former `aead_allow_partial` switch. Use the
[direct test instructions](eip93-test/README.md#dma-boundary-and-burst-experiments)
to hold key/IV/AAD constant while varying length and actual DMA placement.
The logged `process` length is AAD plus payload. Packet DMA output additionally
includes the tag, also on decryption before host tag verification.

Patch 024 adds `dma_maxburst`, a read-only module parameter applied at probe
before requests can reach the engine. Patch 032 changes its default from
`-1` to `6` in release 25; an explicit `-1` still leaves the setting found
at probe unchanged. Values `0..15` select the raw `MXBRSTSZ` field in
DMA configuration register `0x120`, bits `[3:0]`. These are field encodings,
not byte counts. The driver preserves every other bit and logs the before/after
register values and read-back field. Changing the setting requires unloading
and reloading the driver. Reloading with `-1` does not promise to restore an
earlier override; explicitly select the baseline value for comparisons.

The SHARC+ hardware reference, table A-105, identifies register offset `0x120`;
table 44-43 describes `MXBRSTSZ`, 1 KiB boundary splitting and short transfers.
The encoding-to-byte mapping and exact Airoha bus behavior are not established
by that description. Compare the already observed raw settings `6` and `5`
using the same seed, offsets, lengths and repetitions. This is a diagnostic
control. Release 25 selects raw 6 for normal throughput, while raw 5
remains available for comparisons; neither value prevents the observed
short unaligned authenc failure without a source-buffer safeguard.

Patch 025 changes the buffer thresholds independently of `MXBRSTSZ`: register
`0x10c` gets output `0x40` in bits `[23:16]` and input `0x40` in bits `[7:0]`.
Both thresholds are programmed at initialization, before algorithms are
registered, for every supported EIP93 integration. The SHARC+ reference,
table 44-33, describes these fields in bytes and allows multiples of four.
The MTK SDK's older word-count comments do not establish Airoha's units.

On the tested Airoha device, output thresholds 128 and 132 failed case 323,
while 32 and 124 passed; lowering only the input threshold did not fix it.
Measured throughput was unchanged across output thresholds 32, 64, 96 and
124. A full-suite device log from the lowered-threshold experiments passed
all 32 algorithms: 98,888 tests, no failures or skips, and 400,227 async
operations with three repetitions. Those results used the earlier tag path.
A subsequent device run of release 20 also passed the full matrix with three
repetitions, using the final 64/64 defaults and restored packet-tag DMA. The
physical cause remains unconfirmed.

The worker handles skcipher, AEAD and hash requests because they share the
same hardware resources. It keeps multiple requests in flight, up to the
existing 511 request IDs and 511 usable descriptor slots. When either limit
is reached it sleeps until completion returns a credit. Large requests can
span multiple ring passes. Submission no longer uses a timed retry loop.

The software queue admits 511 pending requests before using the Crypto API
backlog contract: an accepted backlog request returns `-EBUSY`, receives an
`-EINPROGRESS` callback when promoted, and later receives its final completion.
Stock kTLS permits backlog and waits for that notification. Callers without
`CRYPTO_TFM_REQ_MAY_BACKLOG` can still receive `-ENOSPC` if the software queue
is full. Allocation and DMA failures remain errors; preparation failures
complete an accepted request exactly once.

Device testing of release 5 passed default TLS 1.3 TX but reported
intermittent TLS 1.2 plaintext mismatches, including both 16,384-byte and
16,368-byte records. The failing record number varied. Patch 010 fixes a
concrete DMA-safety problem that could explain this: word alignment and
AES-block lengths do not prevent separate requests sharing a CPU cache line.
Cleaning a shared line can overwrite another request's DMA output on a
non-coherent SoC. The new check applies to destinations and in-place
buffers mapped `DMA_BIDIRECTIONAL`, covering the full SG entry including
bytes beyond cryptlen. Read-only `DMA_TO_DEVICE` sources do not need this
cache-line restriction: cleaning is non-destructive for them. Their
cipher-block length and SG boundary checks still apply. Release 28 retains
the four-byte source offset check for DES/3DES and all native authenc inputs.
Private bounce mappings cover whole cache lines.

`CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC` is enabled on this target. In Linux
6.18.60, its `dma_kmalloc_needs_bounce()` heuristic considers size and DMA
direction, not the pointer's alignment. It protects small destinations such
as GCM's 16-byte mask block, but skips mappings at least
`2 * ARCH_DMA_MINALIGN` bytes long. Its `DMA_TO_DEVICE` exemption is
intentional and safe for read-only sources. The size heuristic does not
guarantee that a page-sized SG range at an unaligned offset is bounced.
See [dma-map-ops.h](https://github.com/gregkh/linux/blob/v6.18.60/include/linux/dma-map-ops.h).

The TLS 1.2 TX pass with release 7 supports this explanation for the reported
mismatch. Repeated runs are still useful because the original failure was
intermittent, and TX benchmark validation also exercises kTLS RX. The extra
content-type byte in full TLS 1.3 records already sends those requests through
the existing bounce path. Patch 010 may increase destination copying for
block-aligned requests that previously used direct DMA.

Patch 011 removes the AES skciphers' `0xf` alignment masks and the redundant
zero initializers for the other skciphers and AEAD algorithms. The default
mask is zero: the Crypto API no longer sees an additional caller alignment
requirement. Keys and IVs are copied into private storage, and AES key
expansion supports unaligned input. Driver preparation handles DMA mapping
and bounce buffers before submission.

Release 9's patch 012 removed `IS_ALIGNED(sg->offset, 4)` from SG validation.
On Airoha with stock kTLS, `./ktls-bench -v 1.2` then reported
`plaintext mismatch at record 8`; the same command passes with release 8.
Release 10 withdraws patch 012 and restores the aligned bounce path while
retaining the `cra_alignmask` removal and the destination cache-line fix.
Its prepared driver sources are identical to release 8.

Patch 028 retried the SG offset-check removal in release 21, after the
global 64/64 DMA thresholds and restored packet-tag path passed the full
device test matrix. It applies after patches 025–027, leaving patch 012
withdrawn. Device testing of release 21 still failed at multiple cases,
so patch 029 restores the check in release 22. Cache-line
checks still protect destinations and in-place buffers, and cipher-block
length and SG boundary checks remain.

The reported release 21 failure is ECB-DES case 322: seed 1708171853,
16,368 bytes, `byte-offset` layout, first of three repetitions. Only offsets
16,360 through 16,367 differ, exactly the final DES block. The source is one
SG entry starting 129 bytes into its page allocation. The separate destination
starts at offset 131 and is already bounced for cache-line isolation. The
source is newly eligible for direct DMA without the word-offset check.
This request uses one descriptor, no IV and no authentication tag. Corruption
of even one input byte can change the entire final encrypted block; the log
does not identify the underlying DMA fault. The 64/64 thresholds alone do
not resolve this alignment-related regression.

A subsequent full run without stopping at the first failure reported 24
failures among 33,240 tests, with raw `MXBRSTSZ=6`. Every failure used a
`byte-offset` (19) or `cache-edge` (5) layout: each has a single unaligned
source entry. All eight standalone DES/3DES ECB/CBC mismatches affected
exactly the last eight bytes, in either encryption or decryption. Twelve
RFC3686-AES authenc failures shared a one-byte payload, five-byte AAD and
`byte-offset` layout (cases 14/399/784, all three AES key sizes and four
HMAC variants). They changed the payload byte and the entire tag. Four
more failures affected DES/3DES authenc. Standalone AES, hashes/HMACs and
AES-CBC authenc passed this run; that does not establish their immunity.
The short authenc case makes transfer start/end handling a useful target,
alongside the longer DES cases. Actual failing DMA addresses were not logged.

Patch 030 supports a controlled retry without changing the defaults.
`skcipher_unaligned_src=1` skips only the read-only source offset check for
skcipher requests with distinct source and destination SG tables. Destination
cache-line checks, in-place checks, block lengths and authenc remain enforced.
`skcipher_dma_debug=1` logs mappings, bounce use, each descriptor and the final
completion, including the raw DMA configuration and buffer thresholds. Both
parameters are root-only and default to zero; the bypass is diagnostic and
does not establish safe unaligned DMA.

The [skcipher experiment instructions](eip93-test/README.md#unaligned-skcipher-source-experiments)
use test release 5 to hold the destination aligned while sweeping source
offsets 0–7 and lengths around the reported case. `pad_dst=1` pads the mapped
destination extent, with padding still checked for corruption. On an ECB
encryption mismatch, software decrypts the first bad block; on a decryption
mismatch, it re-encrypts it. A byte-difference mask compares the recovered
block with the original input. This separates a
localized recovered-input difference from a whole-block difference without
asserting where the physical corruption occurred.

The device replays subsequently isolated an effective input-tail
substitution: with source offsets 1, 2 and 3, software inversion of the bad
ECB-DES output recovers the final 1, 2 or 3 input bytes from four positions
earlier. Offset 4 passed. The failure also occurs at 1,152 bytes, with one
descriptor and no 4 KiB source or destination boundary crossing. Logging
is not required to reproduce it. These observations identify what the
cipher effectively consumed, not which DMA or bus component substituted it.

With the 64/64 thresholds held fixed, the 1,152-byte replay failed at raw
`MXBRSTSZ=6`, passed at `5`, then failed again on the first repetition at
`6` with the same recovered-input substitution. A subsequent raw-5 sweep
passed 12,000 cases / 24,000 crypto operations: source offsets 1, 2 and 3,
lengths 1,032, 1,152, 2,048 and 16,368, and 1,000 repeats per combination.
The user then reported that the full matrix with three repeats also passed
at raw 5 with `skcipher_unaligned_src=1`. AEAD retained its source check
in those runs; raw 5 is a candidate workaround, not a general alignment
guarantee. Its previously observed TLS throughput cost remains relevant.

Patch 031 adds `aead_unaligned_src=1` for the remaining direct-read test.
Only authenc requests with distinct source and destination SG tables can
bypass the source offset check; the single-entry and block-length checks
still apply. Decryption still uses a private destination until the tag is
verified. Unaligned in-place buffers still bounce because their DMA-written
cache lines must be isolated. Explicit `aead_src_offset` settings still
force a source copy. See the [AEAD source experiment](eip93-test/README.md#unaligned-aead-source-experiments)
for a logged replay followed by a full matrix with both bypasses enabled.

The subsequent device replay of authenc SHA-256/RFC3686 case 14 failed on
its first encryption at both raw 5 and raw 6: one payload byte and all
20 tag bytes differed. The direct source started at page offset 129 and
mapped six bytes (five AAD plus one payload); the destination was bounced
and cache-line isolated. The expected ciphertext byte was `da`, observed
`e8`. With the expected CTR keystream `c4`, that recovers input byte `2c`
instead of `1e`, matching the byte four positions earlier in the seeded
input (`f8 2c cb 55 5e 1e`). This is consistent with the earlier input-tail
substitution, without proving the location of the physical fault.

Disabling the AEAD bypass at raw 5 passed 20 cases / 100 operations, all
with aligned source and destination copies. The 60 expected `EBADMSG`
completions reject deliberately corrupted authentication inputs. This
control changes both source alignment and mapped extent (6 to 64 bytes).
A length sweep with direct sources at raw 6 failed only at payload length
1; lengths 2 through 8 each passed 20 repetitions. Lengths 1 and 5 share
the same end alignment, so that alignment alone is not a sufficient trigger.
Test release 6's [source-padding and offset experiment](eip93-test/README.md#aead-source-padding-and-offsets)
holds the six-byte request fixed while independently varying those DMA
buffer properties. Both driver source bypasses remain disabled by default.

The offset sweep at raw 6 subsequently failed at offsets 1 and 5 with both
six-byte and 128-byte source mappings. Offsets 0, 2, 3, 4, 6 and 7 passed
with either mapping size; each combination ran once. Extending the mapping
alone therefore does not fix the observed case. Both failing offsets span
two partial aligned words without a complete word between them. This
identifies a useful guard condition, not the physical cause.

Patch 033 keeps DES/3DES source offsets word-aligned at the normal raw-6
burst setting, while allowing byte-aligned read-only AES sources. Authenc
gets a conservative short-input exception: unaligned sources bounce when
AAD plus payload is less than eight bytes. The check uses processed length
before adding the received tag, so decryption and padded source mappings
cannot bypass it. Aligned short inputs still use hardware directly. Existing
empty-payload and unrepresentable-AAD software fallbacks are retained.
Destination/in-place cache-line checks and private authenticated-decryption
output are unchanged. The diagnostic source switches can still bypass these
guards for experiments and default to zero.

Release 26 subsequently failed SHA224/RFC3686 authenc case 740 on repetition
3 of a full run: 16,385 payload bytes, 65 AAD bytes, a 28-byte RFC3686 key
(AES-192 plus nonce), a four-byte tag and a source offset of one. Differences
start at SG byte 16,447, covering the final three payload bytes and all four
tag bytes. The single-entry unaligned source qualifies for direct DMA under
the release 26/27 rule; its destination already bounces. This rules out the
assumption that an eight-byte processed input is enough to admit unaligned
native authenc safely.

In a focused device comparison, offset 1 passed 100 repetitions, then failed
on repetition 28 of a second run with the same mismatch. The word-aligned
offset-4 control passed both 100-repetition runs (1,000 crypto operations).
Patch 035 therefore restores source word alignment for all native authenc
lengths. This is a source-buffer workaround pending hardware validation;
the physical fault remains unconfirmed. It keeps raw burst 6, 64/64 buffer
thresholds, byte-aligned AES skcipher sources and the disabled size fallback.
Authenc cipher/hash computation still runs on hardware after the source copy.
The existing diagnostic bypass remains available and defaults to off.

TLS 1.2 GCM ciphertext follows a 5-byte header and an 8-byte explicit nonce,
so RX scatterlists can start at non-word-aligned offsets. Whether a request
uses direct DMA depends on its SG layout. GCM authenticates the input
ciphertext separately from CTR decryption, so a faulty CTR DMA read can
produce wrong plaintext despite successful tag verification. This is
consistent with the regression; the exact hardware failure is not traced.
The SHARC+ manual's ESP padding rule and support for byte-sized DMA tails
do not establish support for arbitrary DMA starting addresses on Airoha.

For the next hardware test, rebuild and install only `kmod-eip93`, keep stock
kTLS, and run with console logging enabled:

```sh
./tls_rx_bench -d tx -v 1.2
./tls_rx_bench -d tx -v 1.2 -s 16368
./tls_rx_bench -d tx -v 1.3
./tls_rx_bench -d tx -v 1.2 -m splice
./tls_rx_bench -d rx -v 1.2
```

The original `Failed to allocate request ID: -28` should no longer occur
under ordinary load. Retain `Failed to allocate request ID: -12` or
`Missing request: ...` messages if they appear. An unmatched completion
still cannot identify or complete its original waiting request; its ID,
flags, control and length words are needed for diagnosis.

Add further driver experiments under `package/kernel/eip93/patches/`.
The package supports the same Airoha and MT7621 targets as the stock driver.
AN7581 and AN7583 already have enabled EIP93 device-tree nodes in this fork.
Other boards still need a suitable enabled device-tree node.

## Select the packages

In `make menuconfig`, select:

- **Kernel modules → Network Support → kmod-ktls**
- **Kernel modules → Cryptographic API modules → kmod-eip93**

Deselect `kmod-tls` and `kmod-crypto-hw-eip93`. For example, the relevant
configuration entries for separately installable packages are:

```text
CONFIG_PACKAGE_kmod-ktls=m
CONFIG_PACKAGE_kmod-eip93=m
# CONFIG_PACKAGE_kmod-tls is not set
# CONFIG_PACKAGE_kmod-crypto-hw-eip93 is not set
```

Run `make defconfig` after editing the configuration. AES-GCM and AES-CCM
dependencies are selected by `kmod-ktls`; EIP93 also selects the cipher and
hash modules needed by its algorithms and software fallbacks.

The package metadata requests `CONFIG_TLS=m`,
`CONFIG_TLS_DEVICE=n`, `CONFIG_TLS_TOE=n` and `CONFIG_CRYPTO_DEV_EIP93=m`.
The in-tree modules may still be compiled during a full kernel build; these
packages install their external replacements instead. Package exclusivity is
handled by the `CONFLICTS` declarations for `kmod-tls` and
`kmod-crypto-hw-eip93`.

If these settings differ from the running firmware, first create and boot a
matching firmware/kernel using the normal PonWrt build procedure. An external
module cannot add missing built-in networking hooks or change the running
kernel's configuration. Keep that build tree for subsequent module builds.

## Rebuild only a module

Once the matching kernel and toolchain have been built, the commands are:

```sh
make package/kernel/ktls/clean V=s
make package/kernel/ktls/compile V=s

make package/kernel/eip93/clean V=s
make package/kernel/eip93/compile V=s
```

Cleaning prepares a fresh private copy and applies the package patches again.
For AN7581, the source copies normally reside in:

```text
build_dir/target-*/linux-airoha_an7581/ktls/net/tls/
build_dir/target-*/linux-airoha_an7581/ktls/include/net/tls.h
build_dir/target-*/linux-airoha_an7581/eip93/
```

Use the corresponding target directory for AN7583 or another supported board.
Use package `prepare QUILT=1`, `quilt new/add/refresh` in the private source
directory, and package `update` to save edits into the package's `patches/`
directory. kTLS patches use paths relative to `ktls/` (such as
`net/tls/tls_sw.c`); EIP93 patches use paths relative to `eip93/` (such as
`eip93-common.c`). Unsaved edits in `build_dir` are lost on `clean`.

Bump the package's `PKG_RELEASE` when distributing an updated build. Keep the
kernel configuration and version unchanged between module iterations. If the
kernel changes, rebuild the modules against that new kernel.

## Install and check

Find the resulting packages under `bin/targets/` or `bin/packages/`:

```sh
find bin -type f \( -name 'kmod-ktls-*.apk' -o -name 'kmod-eip93-*.apk' \)
```

Close connections and other crypto users before replacing loaded modules.
Remove the installed stock package, if present, and install the corresponding
replacement and its dependencies using the device's package manager. For
locally built APKs, `apk add --allow-untrusted /tmp/<package>.apk` permits the
local package signature; it does not bypass the exact kernel dependency.
The packages do not install boot-time autoload entries. Load or reload the
modules explicitly when testing. A busy module cannot be unloaded until its
users have released it.

```sh
modprobe crypto-hw-eip93
modprobe tls
cat /proc/crypto
cat /proc/net/tls_stat
dmesg | tail -n 50
```

EIP93 registration alone does not prove that TLS selected it. The driver
provides AES-CTR and authenc algorithms, not a native `gcm(aes)` or `ccm(aes)`
implementation. Generic GCM/CCM can use its CTR implementation while doing
GHASH/CBC-MAC in software. Check the instantiated AEAD's driver name in
`/proc/crypto`; CPU AES implementations may otherwise win algorithm selection.

The standalone
[`tls_rx_bench.c`](https://github.com/LGA1150/linux/blob/tls-rx-pipelining-v6.18/tools/testing/selftests/net/tls_rx_bench.c)
and existing TLS selftests are available in the Linux branch. The benchmark
accepts `-c gcm|ccm` and `-k 128|256`, with AES-GCM-128 as the default. kTLS
supports AES-GCM-128, AES-GCM-256 and AES-CCM-128 with a 16-byte tag;
AES-192, AES-CCM-256 and CCM-8 are not exposed by its UAPI.

For example, after building the standalone benchmark on your test system:

```sh
./tls_rx_bench -d tx -c gcm -k 256 -j 4
./tls_rx_bench -d tx -c ccm -k 128 -m splice -j 4
./tls_rx_bench -d rx -c ccm -k 128 -v 1.2
```

Run both TLS versions and both directions, including splice, and retain
plaintext verification when comparing results. Cipher selection also applies
to preparation, every worker and the untimed verification pass.

The package and each intermediate EIP93 patch version were checked with host
GCC 14.2 and `W=1` against prepared Linux 6.18 x86-64 headers, including
PonWrt's fallback patch and settings. The final six driver objects compiled
and linked into `crypto-hw-eip93.o`. All package patches apply to the prepared
Linux 6.18.60 driver without fuzz or offsets and reproduce the compiled
sources. Strict checkpatch passes, apart from the generic new-file advisory
for `eip93-queue.c`, which is already covered by the upstream EIP93
MAINTAINERS directory entry.

A host harness compiled the actual queue, ID and ring functions with mocked
kernel primitives and DMA, using AddressSanitizer and UBSan. It exercised
511 occupied IDs, queue overflow and backlog notifications, requests larger
than two rings, 4,096 requests from concurrent producers, injected allocation
and preparation failures, immediate request freeing, and shutdown with work
pending. Every accepted request completed exactly once and all credits were
returned. This tests the submission lifecycle, not cryptographic output or
hardware DMA/cache behavior.

Patch 010 also passed a host test using the actual old/new alignment
helpers: GCM request sizes, full mapped SG extents, cache-aligned fast paths,
and bounce padding within the allocated pages. The revised checks also
verify that read-only sources retain the old hardware alignment behavior
while DMA-written and in-place buffers get cache-line isolation. The test
also uses the actual `dma_kmalloc_needs_bounce()` helpers: the 16-byte
destination is protected,
but a 4,096-byte mapping at offset 16 bypasses that heuristic. A simulated
non-coherent cache-line writeback demonstrates the unsafe large range and
how the driver bounce path isolates it. The test used AddressSanitizer and
UBSan; it does not reproduce the physical device's failure.

These checks do not produce or test a loadable Airoha module. Release 5's
TLS 1.3 TX pass and release 7's TLS 1.2 TX pass are from device testing.
The TLS 1.2 RX comparison (release 8 passes, release 9 fails) is also from
device testing. Release 10 restored release 8's identical driver sources.
Release 11's four additional patches each compile and link with host GCC
and `W=1`, and pass strict checkpatch. Fresh application of all fifteen
patches reproduces the compiled sources without fuzz or offsets.

A host HMAC regression test runs the actual old and new key setup helpers
with OpenSSL-backed synchronous hash stubs. It verifies 288 cases across
MD5, SHA-1, SHA-224 and SHA-256, including RFC 4231 case 6, both pad setup
paths, long and short keys, and truncated and full tags. It reproduces 120
incorrect outputs from the old helper. A second test runs the actual AEAD
core and driver tag-size setters: 108 cases verify supported lengths and
unchanged state on rejection, reproducing 72 silent truncations in the old
driver. Both tests pass AddressSanitizer and UBSan. These tests do not run
the hardware hash engine.

On Airoha, release 11 passed all eight hash/HMAC algorithms in the direct test:
14,080 cases, 89,841 asynchronous operations, no failures or skips, with seed
1708171853. Authenc then failed its first empty-message case with the DMA warning
described above.

Release 12 builds and links the complete external module with host GCC 14.2
and `W=1`. All sixteen patches apply without fuzz or offsets and reproduce
the compiled sources. Patch 017 passes strict checkpatch. A QEMU TCG regression
uses the actual old/new AEAD front ends with cipher submission stubbed to fail
and hardware HMAC pad preparation mocked. The old code reaches the submission
stub for the reported case; the patched code
passes 352 zero-payload cases across all sixteen authenc algorithms without
reaching hardware. These cases cover cipher key sizes, SG layouts, AAD, HMAC
keys, truncated tags, both key/tag setup orders and corrupted-tag rejection.
The test does not emulate EIP93 or DMA. On Airoha, release 12 advances past
the initial empty-message cases to case 55, where a corrupted tag is accepted.

Release 13 builds the complete module with host GCC 14.2 and `W=1`; patch 018
passes strict checkpatch. A QEMU TCG fixture uses the actual AEAD front ends,
SA setup, SG preparation and completion functions, with synchronous software
modeling the basic crypto operation and DMA stubbed. The existing test module
reproduces case 55's corrupted-tag acceptance before the fix, then passes 192
cases across all sixteen authenc algorithms after it. These include all cipher
key sizes, truncated tags, in-place and fragmented buffers, and independently
corrupted tags, ciphertext and AAD. This validates the driver logic against the
model; physical EIP93 digest-output validation remains necessary.

Release 14 also builds with host GCC 14.2 and `W=1`. All eighteen package
patches apply without fuzz or offsets and reproduce the compiled sources;
patch 019 passes strict checkpatch. The model detects release 13 programming
a 12-byte offset for case 57's 13-byte AAD. With both fixes, the unmodified
test module passes its full authenc group: sixteen algorithms, 12,824 cases,
30,292 modeled async operations and no failures or skips. A private test-module
copy substitutes AAD sizes around 255/256 and 1020/1024 (up to 4096 bytes),
passing another 6,488 cases. Both runs use seed 1708171853. These are model
tests, not hardware or noncoherent DMA validation; device results follow.

On Airoha, release 14 passes the first seven authenc algorithms (8,281 cases):
all four AES-CBC/HMAC combinations and RFC3686 with MD5, SHA-1 and SHA-224.
SHA-256/RFC3686 intermittently fails case 281 with either valid-decrypt
`-EBADMSG` or an encryption output mismatch. Its parameters are 1025 payload
bytes, 13 AAD bytes, a 32-byte tag, a 256-byte HMAC key and uneven in-place
scatterlists, seed 1708171853. Five isolated replays initially passed; a later
replay loop also failed intermittently. The cause remains unresolved.
Release 15's diagnostics distinguish hardware status from
host tag comparison and report which digest bytes differ.

The device then reported `different=20000000`, isolating tag byte 29, with
hardware success and a 1070-byte result. An encryption failure confirmed
exactly one differing byte at SG offset 1067: expected `0x29`, actual `0xa8`.
The latter matches neighboring tag byte 28; the remaining payload and tag
bytes match. Before SHA word conversion, this corresponds to packet offset
1068 containing the value from offset 1069, the last byte of the DMA result.
This is consistent with localized digest storage or transfer corruption,
not evidence that the HMAC calculation itself is wrong.

The complete release 15 driver builds with host GCC 14.2 and `W=1`; patch 020
passes strict checkpatch and the nineteen-patch series applies cleanly. QEMU
fault injection into the software crypto model verifies that first/last tag
byte corruption reports `different=80000001` after SHA word conversion.
No firmware, target module, toolchain or benchmark was rebuilt.

Release 16 builds the full external module with host GCC 14.2 and `W=1`.
Patch 021 passes strict checkpatch, and all twenty package patches apply
without offsets or fuzz to reproduce those sources. A QEMU TCG model injects
the observed neighboring-byte duplication into the old packet-tag path,
reproducing the exact offset 1067, expected `0x29`, actual `0xa8` diagnostic.
The saved-state path passes that replay and the existing test module's full
authenc group: 12,824 cases across sixteen algorithms, 30,292 modeled async
operations, no failures or skips, seed 1708171853. Injected payload, saved
digest and hardware errors are still detected.

The model withholds device-written SA state until DMA unmapping and checks
for missing or duplicate unmaps. Twelve additional completion cases cover
ECB, CBC and CTR-wrap state, in-place/separate buffers, and success/error
status. These checks validate driver logic against modeled behavior; they
do not emulate the EIP93 bus or establish that release 16 fixes the physical
fault. Rebuild only `kmod-eip93`, retain test release 3, and rerun the case-281
loop from the test README followed by `group=aead seed=1708171853` on Airoha.

On Airoha, release 16 passed case 281. The next full authenc run passed
7,096 cases across the first six algorithms, then SHA-224/RFC3686 case 323
reported two incorrect decrypted payload bytes after successful tag
verification. It uses 16,368 payload bytes, 63 AAD bytes, a 24-byte tag, a
one-byte HMAC key and the in-place `word-not-cache` layout. The 16,431-byte
DMA output ends with three expected bytes `48 21 b4`. The mismatch spans
offsets 16428 and 16429, with the first byte becoming `b4`, matching the last
output byte. Both buffers already take the aligned bounce path. This
supports a partial-word output problem beyond digest storage; the exact
hardware/interconnect cause remains unconfirmed.
The user later confirmed that case 323 is intermittent and that a full rerun
on release 16 passed. This does not establish a fix or verify release 17.

Release 17 builds the complete module with host GCC 14.2 and `W=1`.
Patch 022 passes strict checkpatch; all twenty-one patches apply without
fuzz or offsets and reproduce the tested driver sources.
A QEMU TCG model repeats the final byte over a partial packet-output word,
reproducing case 323's decrypt mismatch at offsets 16428–16429 before the
guard. The same case passes through synchronous fallback afterward. The
unmodified test module then passes all 12,824 authenc cases with simulated
tail corruption enabled: 14,436 modeled async operations and zero partial
packet-output words reach the hardware model. These tests validate the
fallback routing and exact-message crypto behavior, not the physical bus.
These release 17 checks used test package release 3. No target module or
firmware was built.

Release 18's full driver builds with host GCC 14.2 and `W=1`. QEMU TCG tests
use the actual parameter parser, bounce allocation/free, AEAD, SA preparation
and completion helpers with software crypto and modeled DMA ownership. Six
source/destination offset pairs, including 4032-byte offsets on 4 KiB pages,
pass case-281/323 length sweeps around 1 KiB and 4 KiB with repeated input.
The model checks allocation base/order and missing/double frees, including
injected preparation and completion failures. The default partial-word guard
prevents the modeled case-323 corruption; explicitly bypassing it reproduces
the failure. With offsets 64/128, all 12,824 authenc cases pass (30,292 modeled
async operations). These tests do not emulate the physical bus.

Release 19 also builds with host GCC 14.2 and `W=1`. Both new patches pass
strict checkpatch. All twenty-three patches apply without fuzz or offsets to
the prepared Linux 6.18 driver and reproduce the compiled sources. QEMU runs
the actual burst parameter parser and register update helper against mocked
MMIO: default preservation, raw values 0/5/6/15, unchanged other bits/registers,
out-of-range rejection and read-only sysfs all pass. No target build was run.

Release 20 builds the complete external driver with host GCC and `W=1` after
each of patches 025, 026 and 027, without warnings. All three patches pass
strict checkpatch. Fresh application of all twenty-six package patches has
no offsets or fuzz and reproduces the compiled sources.

QEMU TCG loads and unloads the actual driver module. The existing software
model exercises the final AEAD, SA setup, SG preparation and completion
helpers with packet-tag DMA restored: all 12,824 authenc tests pass, with
30,292 modeled async operations. Cases 281 and 323 each pass three repeats
through hardware submission in the model. Sixty-four shifted-buffer length
trials around 1 KiB and 4 KiB also pass. Corrupted payload/tag results and
injected preparation/completion errors are detected, and IV/state/bounce
cleanup passes. These checks validate driver logic with software crypto;
they do not emulate the physical DMA bus or validate the new 64/64 settings
on hardware. No target module, firmware, toolchain or benchmark was built.

The user subsequently confirmed that release 20 passes the full test matrix
on Airoha AN7581 with this command:

```sh
insmod eip93_test.ko seed=1708171853 repeats=3 stop_on_failure=1
```

This device result covers the 64/64 defaults with both workarounds reverted:
partial-word authenc uses hardware and the engine appends packet tags.
The run uses the existing SG alignment and destination cache-line checks.

Release 21 builds with host GCC and `W=1` without warnings; patch 028 passes
strict checkpatch. All twenty-seven patches apply without offsets or fuzz
and reproduce the compiled sources. QEMU TCG loads the actual module and
passes the existing software model's 12,824 authenc cases with 30,292
modeled async operations, including 2,039 requests retaining byte-aligned
source buffers without a driver bounce. Replays of cases 281/323, shifted
boundary sweeps and injected-error cleanup also pass. The model validates
driver logic, not physical DMA. Subsequent device tests failed at multiple
cases, including ECB-DES case 322; these model passes do not establish safe
unaligned DMA on hardware.

Release 22 builds with host GCC and `W=1` without warnings, and patch 029
passes strict checkpatch. All twenty-eight patches apply without offsets
or fuzz; the prepared sources exactly match the release 20 driver. QEMU TCG
loads the actual module and passes all 12,824 authenc model cases, with
30,292 modeled async operations and no unbounced non-word-aligned sources.
The case 281/323 replays, shifted boundary sweeps and injected-error cleanup
also pass. These checks do not reproduce the device's ECB-DES DMA failure.

Release 23 and test release 5 build with host GCC and `W=1` without warnings.
Patch 030 and the test-source diff pass strict checkpatch. All twenty-nine
driver patches apply without offsets or fuzz and match the compiled sources.
QEMU TCG loads the actual driver with both new controls off. All 33,240 test
cases pass against asynchronous software targets; the model passes 12,824
authenc cases and the ECB matrices using actual driver SG and descriptor
helpers. Offset/length sweeps verify default bouncing, opt-in direct sources,
destination padding and preservation of in-place/authenc checks. Fault
injection validates ECB inverse diagnostics for encryption and decryption
with AES, DES and 3DES, and detects writes deep into destination padding.
This checks software behavior and diagnostics, not the physical DMA fault.

Release 24 builds the complete driver with host GCC and `W=1` without
warnings. Patch 031 passes strict checkpatch. All thirty package patches
apply without offsets or fuzz and exactly reproduce the compiled sources.
QEMU TCG loads the actual module with both source bypasses off, then tests
their independent controls in the software model. All 12,824 authenc cases
pass with the AEAD bypass enabled (30,292 modeled async operations), with
2,039 requests retaining non-word-aligned sources. Focused replays verify
the default bounce path, direct-source opt-in, retained in-place/SG checks,
explicit bounce offsets and 64/128-byte destination cache-line isolation.
The model checks that every authenc decryption still has a private output
buffer and detects injected ciphertext/tag and submission/completion errors.
These tests use the actual driver preparation and completion helpers with
software crypto; they do not emulate the EIP93 bus. Test release 5 is
unchanged. No target module, firmware or toolchain was built.

Test release 6 builds with host GCC and `W=1`, and its source diff passes
strict checkpatch. QEMU TCG passes the unchanged 33,240-case software/cryptd
matrix (170,677 async operations) and 448 additional cases combining source
and destination padding, offsets and lengths through 65,536 bytes. The
driver model confirms that case 14 retains `text=1 aad=5 process=6` while
the unbounced source mapping changes from 6 to 128 bytes. It checks retained
destination protections, default source bouncing, invalid controls and
deep padding-write detection. The driver is unchanged at release 24;
these tests do not emulate the physical bus. No target build was run.

Releases 25 and 26 each build the complete driver with host GCC and `W=1`
without warnings. Patches 032 and 033 pass strict checkpatch. Fresh application
of all thirty-two package patches has no offsets or fuzz and reproduces all
sixteen compiled source files.

QEMU TCG loads the actual release 26 module, verifies the default burst value
6 and disabled source bypasses, and rejects an out-of-range burst setting.
The software model runs the actual register update helper and confirms that
default 6 and explicit overrides preserve unrelated register bits. Using the
actual driver preparation/completion helpers, all 12,824 authenc cases and
1,980 AES/DES/3DES ECB cases pass. Focused tests cover six- and seven-byte
authenc inputs, the eight-byte boundary, received tags, padded mappings,
aligned short inputs and DES/3DES authenc. Counters confirm direct unaligned
AES sources and bouncing of guarded sources. The checks also cover 64/128-byte
destination cache-line isolation, private decryption output and error cleanup.
Modeled input corruption is avoided with the default guards and detected
when the diagnostic bypasses are enabled. These tests validate software
behavior; the final policy still needs hardware validation. Test release 6
is unchanged. No target module, firmware or toolchain was built.

Release 27 builds with host GCC and `W=1` without warnings, and patch 034
passes strict checkpatch. All thirty-three package patches apply without
offsets or fuzz and reproduce the compiled sources. QEMU TCG loads the actual
module, verifies the disabled default, rejects runtime writes to the switch
and accepts an explicit load-time opt-in.

Using the actual driver initialization, dispatch and preparation/completion
helpers with software crypto, the model passes all 1,188 AES-ECB cases with
fallback both disabled and enabled, plus 66 focused threshold cases. Those
cases cover all three key lengths, empty/invalid inputs and both sides of
the 256/512-byte limits. Submission counters confirm 48 hardware-path
operations with fallback disabled versus 18 with the old thresholds enabled.
All 12,824 authenc cases also pass with the switch off. This validates routing
and software behavior, not physical DMA. Test release 6 is unchanged, and
no target module, firmware or toolchain was built.

Release 28 builds with host GCC and `W=1` without warnings, and patch 035
passes strict checkpatch. All thirty-four patches apply without offsets or
fuzz and reproduce the compiled sources. QEMU TCG loads the actual module
with raw burst 6, AES fallback off and the authenc bypass off.

The software model passes all 12,824 authenc cases and 1,188 AES-ECB cases,
plus case-740 source offset/padding controls, short authenc cases and
64/128-byte destination-isolation checks. Counters confirm no unaligned
native authenc source reaches submission, while a 16-byte AES skcipher with
an unaligned source still does. Private decryption output and cleanup after
preparation/completion failures remain checked.

With the diagnostic authenc bypass enabled, a modeled substitution of the
last three input bytes from four positions earlier reproduces the device's
mismatch range (16,447..16,453), counts (three payload/four tag bytes) and
first bad byte (`81` to `ac`). A corresponding valid-decrypt fault returns
`-EBADMSG`. The default guard avoids these injected faults. This supports
the input-tail hypothesis but does not emulate the physical bus or prove
the device workaround. Test release 6 is unchanged; no target build was run.


Release 29 builds with host GCC and `W=1` without warnings. The QEMU TCG
model now runs the actual IV setup, CTR-wrap state split, descriptor
construction and completion for all eight skciphers. Its cipher backend
models a hardware counter that updates only the low 32 bits. Before the
fix, it reproduces all 198 device CTR failure case/repetition pairs. After
the fix, all 3,564 CTR repetitions pass, including 396 exact-end wrap
operations and 2,736 internally split operations. All 6,336 skcipher and
12,824 authenc matrix cases also pass; a second full CTR matrix passes with
128-byte modeled cache lines. These checks cover returned IVs in both
directions and partial blocks through 65,536-byte payloads.

Injecting a final input byte reused from four positions earlier into CBC
case 234 reproduces the device's 496..511 mismatch range and first bad byte
(`5b` to `72`). The aligned control and fault-disabled replay pass. This
supports an input-tail hypothesis, but the current device log does not
contain the entire bad block. No CBC alignment workaround is added. These
are software models, not physical DMA tests. Test release 6 is unchanged;
no target module, firmware or toolchain was built.

Test release 7 adds CBC inverse-block diagnostics in a separate commit,
without changing driver alignment policy or generated cases. Its host GCC
`W=1` build and strict checkpatch pass. QEMU TCG passes all 6,336 skcipher
cases and validates eighteen injected-fault scenarios covering CBC
AES/DES/3DES, first/final blocks, both directions, in-place buffers, output
corruption and ECB compatibility. The case-234 input-tail model recovers
only byte 511 changed from `38` to `46`, matching a read from byte 507.
This remains a hardware hypothesis until the full device diagnostic is
available; the code does not add a CBC source bounce or software fallback.


Release 30 adds opt-in tag-byte and buffer-threshold diagnostics for the
SHA1/CBC case-267 failure. Its actual device replay at raw burst 6 fails the
first valid decrypt at source offset 1 and passes all 20 repetitions at
offset 4. Both decrypt sources use direct DMA. Hardware reports success;
all 12 compared tag bytes differ. The earlier raw-15 replay has the same
outcome. Input-threshold experiments have not established a configuration
that handles all unaligned sources, so the package defaults are retained.

The complete release 30 driver and diagnostic model build with host GCC
and `W=1` without warnings. Patch 037 passes strict checkpatch; all 36
package patches apply without offsets or fuzz and reproduce the compiled
sources. QEMU TCG loads the actual driver with both diagnostics off, raw
burst 6 and AES fallback off. Focused model cases check MD5/SHA1/SHA224/
SHA256 tag bytes and masks, register snapshots, aligned and unaligned
controls, bad-input rejection and silent operation with diagnostics off.

The modeled substitution of input byte 1027 (`d8`) with byte 1023 (`f5`)
changes the SHA1/CBC case-267 tag from `9c5e41454bb18494c430058e` to
`b42de3a74f62d9354f5e95c6`. The release-30 device replay matches both tags
exactly at raw bursts 6 and 5 with input/output thresholds 0/64, a direct
unaligned source and hardware reporting success. Both fail the first valid
decrypt, so raw burst 5 does not avoid this fault. This strongly confirms
the specific substitution in the HMAC input, while leaving its physical
origin within the DMA input path or word-alignment logic unresolved.
The final input word is fully inside the 1,040-byte DMA mapping; only
1,028 bytes are processed, so lack of mapped tail space does not explain it.
The diagnostic change adds no bounce or fallback and retains private
decryption output on authentication failure. Test release 7 is unchanged;
no target module, firmware or toolchain was built.

The five-length RFC3686 CTR/HMAC-SHA1 device sweep keeps source offset 1
and AAD length 4. Payloads 1023 and 1027, whose input ends on a word
boundary, pass all 20 trials each. Payloads 1024, 1025 and 1026 fail all
20 encrypts each, corrupting one, two and three payload bytes respectively
and all twelve tag bytes. Software substitution of the final partial
word's bytes from four positions earlier predicts every reported mismatch
range and byte count, including the first ciphertext byte `4d` to `a3`.
At SG offset 1027, CTR inversion recovers plaintext `d6` instead of `38`,
matching byte 1023. The remaining bad byte values were not printed.

The source-offset controls now show that the partial-word condition is
incomplete. Offset 2 passes lengths 1026 and 1027, offset 3 passes lengths
1025 through 1027, and aligned offset 4 passes all five lengths, with
20 trials per length. The extra passes at offset 2/length 1027 and offset
3/lengths 1026 and 1027 still end in partial words. All failing payload
bytes map to allocation-relative word offset `0x484`; the extra partial-word
passes end in word `0x488`. The data substitution still predicts every
reported failure signature, including eleven changed tag bytes in one
case, but its trigger depends on more than word alignment alone.

Holding length 1024 and contents fixed while shifting the source to
offsets 1, 5, 9 and 13 produces the identical encryption failure in all
80 trials. The bad payload byte moves through allocation offsets `0x484`,
`0x488`, `0x48c` and `0x490`; it is not tied to the earlier `0x484` location.

A broader replay rejects the candidate eight-byte pattern fitted to the
earlier 23 offset/length combinations. Of lengths 1016 through 1044 in
four-byte steps at source offset 1, only 1024 fails all 20 trials; all
seven other lengths pass all 20. The generated final input bytes differ
from their four-byte-back counterparts at every length, so equal data
cannot hide the hypothesized substitution. No longer period has yet been
established.

The broader comparison confirms raw burst 6 and thresholds 0/64. Payloads
256, 512, 768, 1024 and 2048 each fail all 20 encrypts. Their four-byte
neighbors and centers 64, 128 and 192 all pass: 480 trials, 100 failures,
2,000 asynchronous operations. Each failure signature matches substitution
of the final input byte from four positions earlier. The sampled 256-byte
spacing is consistent with the reference manual's 256-byte input buffer;
AAD remains four bytes, so total input length is payload plus four and
the fault's exact counter or hardware stage is unresolved.

Test release 8 adds an independent `aad_length=0..1024` override for an
explicit AEAD algorithm and matrix case. It retains the key, IV, tag, layout
and seeded stream prefix; the default keeps the original matrix unchanged.
The device control crosses AAD 0/4/8 with payload 252/256/260 and fails
only when their sum is 260. All three such combinations fail 20/20;
all six others pass 20/20, for 180 cases and 620 asynchronous operations.
The three failures share the same generated input bytes and all match
byte 259 changing from `14` to byte 255's `5c`. This establishes dependence
on total processed input length in this grid, rather than payload length
256 alone. Raw burst 6 and thresholds 0/64 are confirmed by readback.

The raw-burst-5/raw-burst-6 comparison produces identical results at
thresholds 0/64 and AAD length 4. For each setting, unaligned payload 256
fails 20/20 with the same final-input-byte substitution; the other eight
lengths around 64, 128 and 256 pass 20/20. Aligned controls at all three
centers pass. The readbacks confirm both raw settings; the complete test
has 366 cases, 40 failures and 1,670 asynchronous operations. Raw 5 does
not shift the failure to payload 128 in this comparison.

The plain RFC3686-CTR comparison passes at 256, 260 and 264 bytes, while
native SHA1/RFC3686 authenc with zero AAD fails all 20 unaligned 260-byte
trials. Aligned authenc controls and unaligned 256/264-byte trials pass.
The readbacks confirm raw burst 6 and thresholds 0/64. However, the plain
cipher's DMA trace shows `src_copy=1`, a page-aligned source and a 320-byte
mapping for the 260-byte request. Its SG validation uses AES's 16-byte
block size, whereas CTR authenc uses one byte. The passing plain-cipher
result therefore does not isolate the cipher/hash combination as the cause.

Release 31's `skcipher_partial_src` switch allows a direct partial-block
read for an out-of-place AES CTR skcipher request contained in one source
SG entry. It is off by default and retains destination, in-place and
multi-entry SG checks. Patch 038 changes neither the descriptor's crypt
length nor normal behavior. The corrected comparison in the test README
requires a release-31 driver rebuild; test release 8 can be reused. Device
results with the new switch now confirm the same corruption in plain CTR.
At raw burst 6 and thresholds 0/64, both plain CTR and zero-AAD authenc
fail all 20 unaligned 260-byte trials, while their aligned controls and
unaligned 256/264-byte trials pass. The plain-cipher trace shows direct
source DMA, one 260-byte descriptor and hardware success. Its single
wrong ciphertext byte (`7e` to `3f` at 259) matches input `a1` being replaced
by byte 255's `e0`; hashing is not required. The traced input range
`0x83fa5081..0x83fa5184` crosses neither a 1 KiB nor a 4 KiB boundary.
Including the diagnostic trial, there are 241 cases, 41 failures and
641 asynchronous operations.

The follow-up device report confirms that both `pad_src=0` and `pad_src=1`
fail, with raw burst 6 and thresholds 0/64 unchanged. Reported tested plain
RFC3686 lengths of the form `256*n+4` through 2052 bytes all fail at source
offset 1. Extending the source mapping does not avoid these failures;
per-length mismatch traces and repetition counts were not supplied.

Such a source span contains three bytes before word alignment, `256*n`
bytes in complete words and one trailing byte. The padded plain-CTR grid
at lengths 257 through 264 now matches the predicted failure windows:
260-262 at offset 1, 259-261 at offset 2, and 258-260 at offset 3. Every
failing cell fails all 20 encrypts; every other cell passes, including all
160 trials at offset 4. The first bad SG byte shifts from 259 to 258 to
257; each first value matches input from four positions earlier. The
registers remain raw burst 6 and thresholds 0/64. The traced source is
direct and maps 384 bytes while the descriptor processes 260. Including
that diagnostic trial, the totals are 641 cases, 181 failures and 1,101
asynchronous operations. This measures the alignment/count interaction
without identifying the precise hardware stage or a general safe rule.

The short plain-CTR device report confirms that lengths 4/5/6 fail at
source offset 1 while 1/2/3/7/8 pass; repetition counts were not supplied.
A literal FIFO wrap is not necessary, and splitting a four-byte remainder
would still leave a failing operation. The earlier six-byte authenc case
14 also matches four-byte-back input reuse. Release 32 therefore adds
patch 039's opt-in source-end alignment experiment: logical lengths 4..6
process 7 bytes and 260..262 process 263 bytes at source offset 1. Existing
mapped source padding supplies the extra bytes, while private output and
logical-length IV accounting preserve the caller's results. The release-32
device replay now passes all 6,402 cases and 12,804 asynchronous operations
at raw burst 6 and thresholds 0/64. Traces confirm direct sources, private
output and one 7-byte or 263-byte descriptor for logical lengths 4 and 260
in both directions. All 100 repetitions at lengths 1..8 and 257..264 pass
for source offsets 1/2/3/4, including the previously failing windows. This
validates the measured RFC3686-CTR/AES-128 cases without a source copy.
The normal-threshold follow-up also passes: 60,001 cases, zero failures
and 120,002 asynchronous operations at raw burst 6 and thresholds 64/64.
It covers both CTR variants, all AES key sizes, offsets 1/2/3/4 and 25
lengths through 65,520, with 100 repetitions per combination. The traced
16-byte ordinary-CTR request processes 19 bytes in a 16+3 descriptor split,
using a direct source and private output in both directions. Returned-IV
comparisons pass despite the extra AES block and low-32-bit counter wrap.
This validates the current experiment with padded single-entry sources;
concurrent requests, general unpadded/SG sources and other cipher modes
remain outside this replay. Release 32 and test release 8 are unchanged.

The release-31 driver and model build with host GCC and `W=1` without
warnings; patch 038 passes strict checkpatch. All 37 package patches apply
without offsets or fuzz after the distribution fallback patch. QEMU TCG
checks the actual module's default-off switch and passes 385 model cases
(750 asynchronous operations), including 29 mapping checks. The diagnostic
changes the 260-byte plain CTR source from copied to direct while retaining
one 260-byte descriptor and the destination checks. Partial lengths through
65,536, all AES key sizes, counter wrap, SG/in-place guards and the focused
comparison pass. This validates software paths, not hardware DMA behavior.

The release-32 driver and model also build with host GCC and `W=1` without
warnings. QEMU TCG passes 774 model cases (1,490 asynchronous operations),
18 source/destination mapping assertions and four missing-capacity rejection
configurations. The matrix exercises 1,072 extended tails, including 152
that consume another AES block, and checks logical output/IV preservation,
source padding, counter wrap and unchanged paths. Patch 039 passes strict
checkpatch; all 38 package patches apply without offsets or fuzz. The real
module loads with `skcipher_tail_align=N`. These local checks did not build
an OpenWrt target or emulate physical DMA; the separate device replay is
recorded above.

The release-33 driver and model build with host GCC and `W=1` without warnings.
QEMU TCG passes 1,085 model cases (2,112 asynchronous operations), 19 mapping
assertions and two changed-DMA-offset rejection configurations. The model
executes 1,530 reads beyond the recorded source DMA span while keeping source
SG metadata and lengths unchanged. Tests cover unpadded inputs through 65,536,
all AES keys, 1 KiB/page-ending words, counter wrap, returned IVs and unchanged
paths. Patch 040 passes strict checkpatch; all 39 package patches apply without
offsets or fuzz. No OpenWrt target build was run locally.

The release-33 unpadded device replay now passes all 74,403 cases with zero
failures and 148,806 asynchronous operations at raw burst 6 and thresholds
64/64. Both CTR variants, all AES key sizes, source offsets 1/2/3/4 and 31
lengths through 65,536 pass 100 repetitions per combination with `pad_src=0`.
Traces retain source DMA lengths 4/260/16 while processing 7/263/19 bytes,
with `src_copy=0 dst_copy=1` in both directions. Ordinary CTR's 16+3
counter-wrap split and returned-IV comparisons pass. This validates the
single-entry source-overread path on the device; fragmented sources,
concurrent requests and other modes remain outside this replay. Driver
release 33 and test release 8 are unchanged.

Release 33's new path is not exercised by ordinary kTLS AES-GCM records. Generic
GCM prepends a 16-byte tag-mask entry to the payload SG; generic CCM likewise
prepends a tag entry. The current diagnostic checks for exactly one source
entry before any bounce, so these requests keep `tail=0` even when EIP93
performs the CTR work. Multi-entry support is required before a kTLS run can
validate this change. The test README includes a short
[routing check](eip93-test/README.md#testing-the-ctr-overread-through-ktls).

The subsequent TLS 1.2 RX benchmark reports intermittent plaintext mismatches
at input threshold `0x40` with both `skcipher_partial_src=Y` and
`skcipher_partial_src=N`. The diagnostic is not a necessary trigger. Passing
runs at input threshold `0x40` are also reported slower than at input threshold
zero. The routing trace confirms that all record requests retain `tail=0`;
its parameter readbacks are `skcipher_partial_src=Y`, `skcipher_tail_align=N`
and `aes_sw_fallback=N`. It shows direct 16,384-byte payload descriptors
starting at unaligned addresses in both directions, after the separate
16-byte GCM prefix. This exposes input tails outside the current single-entry
workaround. These
multi-entry, block-multiple records take the same eligibility path with
either setting of `skcipher_partial_src`, consistent with the rerun. The trace
does not capture a failing record or prove the corruption's cause. RX IDs
0, 1 and 2 are outstanding concurrently. Benchmark ciphertext preparation
also uses kTLS TX, leaving the direction of corruption unresolved. See the
[diagnostic details](eip93-test/README.md#tls-12-with-the-partial-source-diagnostic).
No driver defaults or package releases change for this report.

Release 34 adds the opt-in multi-entry CTR path in patch 041. It uses the
mapped source segments without copying input, prepares an independent
read-only IV state for each descriptor and discards extra output from
separate padded slots. Logical counters remain continuous across tails,
low-counter wrap and the descriptor length limit. The current hardware
replay instructions are in the
[multi-entry test section](eip93-test/README.md#multi-entry-ctr-tail-alignment).

The driver, software model and focused probe build with host GCC and `W=1`
without warnings. Patch 041 passes strict checkpatch, and all 40 package
patches apply without offsets or fuzz and reproduce the tested sources.
QEMU TCG passes 3,493 existing driver-test cases (6,796 asynchronous
operations), 2,606 focused request checks, 33 benchmark runs and 28 kTLS
selftests. The focused checks include DMA-coalesced entries, intermediate
unaligned tails, AES key sizes, counter wrap, the 20-bit descriptor limit
and preparation/completion errors. The kTLS matrix covers both TLS versions,
GCM-128/GCM-256/CCM-128, recv/send/splice and concurrent sockets. Traces confirm
the new GCM path processes an unaligned 16,384-byte payload as 16,387 bytes.
These tests validate driver logic and integration with software execution;
they do not emulate the physical EIP93 DMA fault or overlapping hardware DMA.
The subsequent hardware trace confirms that patch 041 extends unaligned
16,384-byte payloads by one byte on TX and three bytes on RX, with no source
copy for those requests and word-aligned input ends. Two of four records
in each direction use the new path, and overlapping RX IDs confirm
pipelining. The user then reports that the earlier TLS 1.2 RX workload no
longer fails at input threshold `0x40`. This supports the workaround at
raw burst 6 and thresholds 64/64; run counts were not supplied.
Broader hardware coverage remains to be reported, and benchmark RX preparation
also uses kTLS TX. The diagnostic stays opt-in. Test module release 8 is
unchanged, and no OpenWrt target build was run locally.

The follow-up benchmark comparison with `-j 2` reports median throughput
of 0.883327 Gbps at input threshold `0x40` and 0.973475 Gbps at input
threshold zero. Zero is 10.21% faster in that comparison, despite the
correctness improvement at `0x40`. Full CSV rows were not supplied; the
comparison does not establish the hardware mechanism behind the gap.
TLS 1.3 RX subsequently passes at both input thresholds, with an approximately
equal median of 0.706526 Gbps at each using `-j 2`. This is about 27.4% below
TLS 1.2 at input zero and 20.0% below TLS 1.2 at `0x40`. The current TLS 1.3
recv path uses kernel plaintext buffers and waits for batches of up to eight
records, whereas TLS 1.2 recv retains direct-to-user asynchronous delivery.
Extra copying and batch overhead are candidates, not profiled causes.
The follow-up RX splice medians are 0.752420 Gbps for TLS 1.2 and
0.718228 Gbps for TLS 1.3; the input threshold was not restated. The
version gap falls to 4.54% when both use the kernel-buffer batch path.
TLS 1.3 splice is approximately 1.66% faster than its earlier recv result.
This supports investigating recv-path differences, but pipe work, syscall
counts and prefill also differ, so the comparison does not isolate copying
from batching or scheduling costs.
The driver defaults remain unchanged; broader cipher and I/O-mode coverage
remains to be reported.

Release 35 adds patch 042 for CTR source entries that end inside an AES
block. Hardware handles full spans and the final block when it starts at
a logical block boundary; synchronous AES supplies keystream bytes for
split blocks. Adjacent software spans are combined, and CPU source reads
start after DMA unmapping. Every software span is finished in private
output before any result is copied to the caller, including aliased SG
tables. Requests containing only split blocks complete through the existing
worker without a hardware descriptor. Source mappings stay unchanged, with
no source bounce or private source SG. The option remains default-off;
single-entry, same-table in-place and other cipher paths retain their
existing behavior. See the
[partial-entry replay](eip93-test/README.md#ctr-across-partial-sg-entries).

Host GCC `W=1` builds and Linux 6.18 strict checkpatch pass. All 41 package
patches apply without offsets/fuzz and reproduce the tested driver sources.
QEMU TCG passes 3,493 existing driver-test cases, 21 mapping assertions,
21,988 focused encrypt/decrypt checks and 34 expected error completions.
These include all AES keys, both CTR variants, counter wrap, aliased/merged
SGs, 4,096 chained one-byte entries, descriptor-length splits, and TLS
records fragmented at MSS 1460/1448/1440/1428 with header offsets and tag
padding. The kTLS tests pass 42 benchmark runs and 30 selftests, including
nine runs and two tests at loopback MTU 1500. The latter trace confirms
software boundary handling with direct source DMA on TLS 1.3 RX. These are
software-model checks; subsequent device CTR results are recorded below.
No OpenWrt target build was run, and the test module remains release 8.

The release-35 device report passes both complete `ctr(aes)` and
`rfc3686(ctr(aes))` matrices plus focused aliased/different-split SG replays.
With seed `1708171853`, `max_size=65536` and three repetitions, each algorithm
passes 3,564 matrix cases and 42 focused cases. The six loads total **7,212
case repetitions and 14,028 asynchronous operations, with zero failures or
skips**. Focused cases 266/269 use lengths 2/7/17/31/33/260/16385. The upload
contains results for these two algorithms, without module parameter/register
readbacks or DMA routing traces. MTU-1500 device kTLS results remain to be
reported; normal driver defaults are unchanged.

With MTU unchanged, `./ktls-bench -d rx -v 1.2 -j 2 -p 0 -w 0 -r 5` reports
median throughput of 0.889273 Gbps with tail alignment off and 0.790368 Gbps
with it on at input threshold `0x40` (-11.12%). At input threshold zero,
the corresponding medians are 0.976430 and 0.896518 Gbps (-8.18%). Zero
improves throughput by 9.80% with the option off and 13.43% with it on.
This measures the entire optional path; it does not isolate software
boundary AES. The planner, private output, independent counter states and
per-chunk destination SG walks add work, and preserving source fragmentation
can require more descriptors than the legacy combined-source path. Those
costs have not been profiled on the device. See the
[comparison and optimization candidates](eip93-test/README.md#ctr-across-partial-sg-entries).
Tail alignment remains default-off.

Release 36 adds patch 043 to eliminate intermediate CTR output gaps and
repeated copyback walks. Descriptors write at their logical offsets, so
later ordered packet writes replace preceding overread output. Software
boundary spans run after DMA unmapping, then one `sg_copy_from_buffer()`
copies the original result length. The allocation covers the largest
write extent even when only a short software tail follows a hardware
chunk. Source mappings/read lengths, independent counters and the existing
queue are unchanged. The ADSP-SC58x/ADSP-2158x hardware reference documents
the required sequencing on pages 44-27 and 44-36. Subsequent Airoha overlap
and throughput reports are recorded below. See the
[device replay and unchanged-MTU comparison](eip93-test/README.md#contiguous-ctr-output).

Host GCC `W=1` builds and strict checkpatch pass. All 42 package patches
apply without offsets/fuzz and reproduce the tested driver. QEMU TCG
passes 3,494 existing cases, 22 mapping assertions, 25,060 focused
encrypt/decrypt checks and 34 expected error completions. The model checks
output guards and one copyback per successful request, including hardware
and software overlaps, short tails, aliasing, counter wrap and DMA merging.
An intentionally delayed earlier tail write is detected by comparison
with the software cipher. Existing kTLS coverage passes 42 benchmark runs
and 30 selftests, including MTU 1500. These are software-model results;
no release-36 device performance gain is claimed. No OpenWrt target build
was run, and the test module remains release 8.

The release-36 device trace confirms overlapping hardware output for case
271 at 4,097 bytes, with raw burst 6 and thresholds 64/64. Both encryption
and decryption pass with `text=4097 process=4106 tail=9 sw_bytes=0
output=4100`, direct source DMA and six descriptors. Descriptor 2's 1,027
bytes starting at output offset 1024 overlap descriptor 3's start at 2048
by three bytes; descriptors 4/5 overlap at the final partial block too.

The uploaded full CTR matrices and focused replays then pass **14,328
case repetitions and 28,260 asynchronous operations**, with zero failures
or skips across ten loads. Each CTR algorithm runs cases 266/269/271/273
at nine lengths with 100 repeats, plus three complete matrix repeats;
seed is `1708171853` and `max_size=65536`. The separately pasted one-case
trace is excluded from these totals.

Throughput is reported unchanged after patch 043. The subsequent five-run
CSV comparison with the same TLS 1.2 RX command gives medians of 0.881289
Gbps off and 0.788956 Gbps on (-10.48%). Mean elapsed time increases from
0.612464 to 0.680219 seconds per 64 MiB. Summed receiver CPU time rises
from 0.620072 to 0.665221 seconds, while system busy time averages 1.22
versus 1.23 seconds. The input threshold was not restated with these CSV
rows. Both settings verify plaintext with 64 receive calls and no prefill.

System busy time includes asynchronous workers, interrupts and unrelated
activity; receiver CPU time does not measure the crypto workers. The near
unchanged total busy time does not identify the bottleneck or establish
accelerator utilization. The single copyback still copies all plaintext;
remaining preparation, mapping and descriptor costs have not been profiled
on the device. See the
[timing breakdown and accounting limits](eip93-test/README.md#contiguous-ctr-output).
Normal defaults remain unchanged.

The test module builds with host GCC and `W=1` without warnings and passes
strict checkpatch. QEMU TCG passes 803 case repetitions and rejects six
invalid parameter combinations. These checks cover software and cryptd
targets, the existing driver-function model, all 16 native authenc algorithms,
AAD boundaries through 1024, empty/invalid payload lengths, SG/in-place
layouts and AES key sizes. Model mapping diagnostics confirm all nine
focused AAD/payload combinations use direct source DMA and the intended
processed lengths. These checks do not emulate the physical DMA fault.
No target module, firmware or toolchain was built.
