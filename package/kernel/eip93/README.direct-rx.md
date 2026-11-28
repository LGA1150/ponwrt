# Fragmented input and aligned output experiment

Use `kmod-eip93` release 42, `kmod-ktls` release 5 and `kmod-eip93-test`
release 12. This experiment retains the original ciphertext fragments and
writes plaintext into separate aligned kTLS pages. It removes three
full-record copies on eligible buffered receives:

1. kTLS's private ciphertext copy (`rx_inplace=0`).
2. The EIP93 source bounce (`skcipher_tail_align=1`).
3. The EIP93 output bounce/copyback (`skcipher_direct_dst=1`).

`rx_aligned_output=1` aligns the kTLS destination payload without copying the
input. `skcipher_direct_dst` is opt-in and requires `skcipher_tail_align`.
Custom `gcm-aes-eip93` supplies bulk-only CTR SGs, excluding AAD, authentication
tag and the final partial cache line. Existing connections retain their AEAD
transform; use new connections after loading the updated modules.

The direct CTR planner walks both mapped SGs. Every descriptor, including
word-alignment padding, stays within both mappings and the request length.
Padded output overlaps later logical output; software CTR fills split blocks
and terminal spans after all DMA completes. No additional source mapping or
private source SG is created. Destination extents must own whole DMA cache
lines. Requests whose source and destination share any page, or whose
output is unsuitable, retain the old bounce behavior. Defaults are unchanged.

This removes full-record staging copies, not every small copy: GHASH handles
fragment boundaries, and software CTR handles boundary blocks and GCM's tail.
An ordinary TLS 1.2 direct-to-userspace `recv()` can still need an output
bounce. TCP parser pressure/recovery can also require its existing copy mode.
The new aligned allocation applies to buffered receive, including splice and
TLS 1.3 buffered recv. These paths never make cloned TCP ciphertext writable.

## Hardware correctness first

Keep the existing DMA burst and threshold settings unchanged. On the router:

```sh
p=/sys/module/crypto_hw_eip93/parameters
t=/sys/module/tls/parameters

echo 1 > "$p/skcipher_tail_align"
echo 1 > "$p/skcipher_direct_dst"
echo 0 > "$t/rx_inplace"
echo 1 > "$t/rx_aligned_output"

echo 1 > "$p/skcipher_profile"
echo 0 > "$p/skcipher_profile_min_bytes"
echo 0 > "$p/skcipher_profile_stats"
insmod eip93_gcm_sg_test repeats=3
cat "$p/skcipher_profile_stats"
rmmod eip93_gcm_sg_test
echo 0 > "$p/skcipher_profile"
```

The module explicitly selects `gcm-aes-eip93`. Its 1,944 checks per repetition
cover AES-128/192/256, 5/13-byte AAD, lengths 1 through 65,536 bytes including
16,384/16,385, source fragments of 17/1440/1448/1460 bytes, and offsets 0/1/3.
AAD is separate; destination payload pages are aligned and disjoint. A
synchronous software cipher generates pseudorandom plaintext and expected
ciphertext. Every operation checks data, source preservation and guards;
invalid tags must leave the output unchanged. This module tests the crypto
path directly, without TLS. It does not change driver parameters/registers.

Without other EIP93 traffic, the direct run should show only `ctr_sg` requests
with `src_copies=0`, `dst_copies=0` and `copy_ns=0`. Short GCM messages handled
entirely in software produce no child CTR request. `sw_bytes` counts the CTR
planner's boundary work, not GCM's separate software tail or GHASH buffering.
If the hardware reports a mismatch, retain the log before throughput testing.

## kTLS measurement

For a short local check, after the correctness matrix passes:

```sh
echo 1 > "$p/skcipher_profile"
echo 0 > "$p/skcipher_profile_stats"
/tmp/ktls/ktls-bench -d rx -v 1.3 -j 2 -p 0 -n 257 -w 0 -r 1
cat "$p/skcipher_profile_stats"
echo 0 > "$p/skcipher_profile"
```

Inspect the **decrypt** rows: the benchmark prepares ciphertext using the
same engine before receiving it, so encrypt-row bounces are unrelated to RX.
Compare `src_nents`, `descriptors`, `sw_bytes`, `src_copies` and `dst_copies`.
A source-entry count larger than the request count confirms fragmented input.
Both copy counts should be zero for eligible buffered records. Driver counters
do not measure TCP parser copies; use the existing router-wide perf capture
for the end-to-end comparison and check for `tls_copy_ciphertext` /
`tls_strp_skb_copy` / `tls_strp_copyin_frag` paths.

Then repeat the existing PC test with profiling disabled. Substitute the
router's reachable address for the documentation address below:

```sh
./tls-proxy-bench --connect 192.0.2.20:8443 --listen 0.0.0.0:5001 \
  --insecure -d rx -v 1.3 -j 2 -n 256M -w 1 -r 5
```

Use the same router collector and keep results under `/tmp/ktls/`.
The comparison baseline is the prior aligned in-place configuration:

```sh
echo 0 > "$p/skcipher_direct_dst"
echo 0 > "$p/skcipher_tail_align"
echo 0 > "$t/rx_aligned_output"
echo 1 > "$t/rx_inplace"
```

## Local validation

Host GCC `W=1` builds the driver, TLS module and test module. QEMU TCG uses
the actual GCM wrapper and CTR preparation/completion code with a software
DMA/crypto model and a simulated 64-byte DMA cache alignment. The focused
GCM matrix passes with direct output disabled and enabled. With it enabled,
1,008 child requests record zero source bounces, destination bounces and
copybacks; the model checks descriptor read/write bounds. Repeating the GCM
matrix with simulated 128-byte cache alignment also passes, with 720 child
requests and zero full-record copies (more short messages stay in software).

A CTR regression probe covers separate and DMA-merged source entries, all
AES key lengths, counter wrap, descriptor limits, partial blocks, overlapping
caller SGs, and software-only requests. It passes 25,060 operations plus 24
mapping-failure injections. The overlapping/unaligned controls exercise the
existing bounce path.

TLS 1.2/1.3 recv/splice trials and six boundary/queued-rekey selftests pass.
For TLS 1.3, instrumented kernel counters show zero private ciphertext and
parser-copy events; driver decrypt counters show zero source/destination
copies and nonzero fragmented input. The software model is not EIP93 DMA or
a throughput prediction. Hardware confirmation is still required. No OpenWrt
package/image build was performed.
