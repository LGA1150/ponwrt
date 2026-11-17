# Testing kTLS and EIP93 outside the kernel source tree

These packages build replaceable kernel modules from a private copy of the
target's **prepared Linux 6.18 sources**:

| Package | Build target | Module | Conflicts with |
| --- | --- | --- | --- |
| `kmod-ktls` | `package/kernel/ktls` | `tls.ko` | `kmod-tls` |
| `kmod-eip93` | `package/kernel/eip93` | `crypto-hw-eip93.ko` | `kmod-crypto-hw-eip93` |

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
including PonWrt's small-request software fallback patch. The fallback
configuration matches the stock package (generic: 256 bytes; AES-128: 512
bytes). Release 5 added async backpressure after the device test confirmed
`Failed to allocate request ID: -28` under the default TX benchmark. The IDR
checks in release 4 prevented the NULL-request panic, but returned `-ENOSPC`
to stock kTLS, which aborted transmission with `EBADMSG`.
Release 7 added cache-line isolation to DMA-written buffers, and the default
TLS 1.2 TX benchmark passed on the device. Release 8 removed the algorithms'
`cra_alignmask` initializers. Release 9 also removes the 4-byte SG offset
check, retaining cipher-block length checks and DMA-written cache-line
isolation.

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
| 012 | Remove the 4-byte SG offset check; retain block-length checks and DMA-written cache-line isolation. |

Apply the complete series through 012 for the next test. Patches
004–008 make preparation, completion and ring reuse safe for deferred
submission; they are separate so these changes can be reviewed and bisected.
The previous IDR-only package remains available at
[revision f600a23](https://github.com/LGA1150/ponwrt/tree/f600a2304d24ad8e8f403c10789a8d107cac9e6f/package/kernel/eip93).

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
cipher-block length checks still apply.
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

Patch 012 removes `IS_ALIGNED(sg->offset, 4)` from the SG validation helper.
The SHARC+ packet-engine manual's 4-byte minimum concerns ESP padding;
the driver uses basic operations with stream padding disabled. Its source
and destination registers describe byte addresses, and its DMA controller
supports byte transfers. Read-only source offsets no longer trigger a
bounce solely because they are not multiples of four. Cipher-block length
checks and cache-line isolation for DMA-written/in-place buffers still
apply. The host build cannot validate unaligned DMA on Airoha; device tests
should include source offsets 1, 2 and 3 with otherwise block-sized data.

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
Release 9's relaxed alignment checks still need device validation.
No firmware, toolchain or benchmark was rebuilt.
