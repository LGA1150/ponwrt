# GHASH throughput benchmark

`kmod-ghash-bench` provides `ghash_bench.ko`, an explicitly loaded benchmark
of the kernel Crypto API's `ghash` implementations. It uses
`crypto_ahash_digest()`, so a synchronous shash implementation is measured
through the same ahash frontend used by generic GCM. It does not depend on
kTLS, the EIP93 driver or the standalone hardware test, and does not touch
EIP93 registers. The normal EIP93 driver may remain loaded.

On ARM64 the default driver list is `ghash,ghash-generic,ghash-neon`.
From release 2 the package selects the separate
[`kmod-crypto-ghash-neon-shuffle`](../crypto-ghash-neon-shuffle/README.md)
comparison module on ARM64. Release 17 of that module supplies
`ghash-neon-shuffle`, `ghash-neon-sched`, `ghash-neon-gpr`, `ghash-neon-2x`
and `ghash-neon-2x-pmull`, plus `ghash-neon-pmull` without aggregation
and `ghash-neon-orig-pmull` with only the stock loop's reducer changed.
`ghash-neon-fused` precomputes part of reduction into the hash key, and
`ghash-neon-2x-reuse` retains the last key table between PMULL block pairs.
`ghash-neon-2x-load` also overlaps remaining key loads with multiplication.
`ghash-neon-4x` accumulates four blocks before reconstruction and reduction.
`ghash-neon-8x` extends that to eight blocks with three input passes and no spills.
`ghash-neon-orig-pmull13` tests a 13-instruction reducer in the original loop.
`ghash-neon-pmul` and `ghash-neon-umull` test byte polynomial and integer
widening products, respectively, with the same byte-PMULL reducer.
`ghash-neon-orig-pmull12` combines byte extraction and rotation in a
12-instruction reducer.
`ghash-neon-4x-pmull12` applies it to four-block aggregation.
`ghash-neon-4x-pmull13` compares its UZP/EXT version in the four-block loop.
Select them explicitly alongside `ghash-neon`.
The default driver list is unchanged.
On other architectures the default list is `ghash,ghash-generic`.

The Linux 6.18 kernel patch
[`910-crypto-arm64-ghash-aggregate-four-blocks.patch`](../../../target/linux/generic/pending-6.18/910-crypto-arm64-ghash-aggregate-four-blocks.patch)
updates the standard `ghash-neon` driver to the four-block implementation
from `ghash-neon-4x-regs0`. It keeps the original single-block helper for
short inputs and tails, uses an 80-byte key context, and retains the
post-indexed loads and 296-instruction loop. After rebuilding the kernel
or `ghash-ce.ko`, `drivers=ghash-neon` measures this implementation; the
earlier measurements below used the original single-block driver.
Installing the separate comparison module does not apply this kernel patch.

The stock `ghash-neon` may be built into the kernel. The benchmark checks
for it before attempting to load `ghash-ce.ko`. If it is modular, install
the optional `kmod-crypto-ghash-arm64` package, which supplies that module.
The benchmark loads it before allocating algorithms when needed, so the
default `ghash` comparison sees registered stock NEON. The benchmark no
longer forces that package or its kernel configuration, allowing use with
an existing built-in implementation. The benchmark and new comparison
module have no autoload entries.

Linux 6.18 registers `ghash-neon` from the stock `ghash-ce` on CPUs with SIMD
but without PMULL. It uses ordinary NEON polynomial multiplication and does
not require the optional PMULL crypto extension. On CPUs with PMULL this
particular module instead registers fused AES-GCM algorithms; it does not
register standalone `ghash-neon`. On such a CPU, select available drivers
explicitly, for example `drivers=ghash,ghash-generic`.

Each requested name and the resolved driver name are printed. If `ghash`
resolves to `ghash-neon`, those two rows are repeated measurements of the
same implementation, not different algorithms. All requested drivers must
allocate successfully before any throughput measurements begin. An absent
explicit driver produces an error rather than substituting another one.

## First comparison

Select `kmod-ghash-bench` under Cryptographic API modules and install it
with its dependencies. Run on the device:

```sh
insmod ghash_bench drivers=ghash,ghash-generic,ghash-neon \
    lengths=1440,16384,16400,65536 depths=1 \
    duration_ms=1000 warmup_ms=100 runs=5
dmesg | sed -n '/ghash_bench:/p' > /tmp/ghash-bench.log
rmmod ghash_bench
```

The source buffers start at an alignment of at least 128 bytes. Each size
uses one SG entry by default. Allocation, deterministic software-AES payload
generation, GHASH key setup, reference digests, warmup and verification are
outside the timed interval. Timing includes each digest's initialization,
processing and finalization, request submission, completion handling, and
draining all outstanding requests. Every message starts from a zero GHASH
accumulator; this is not one indefinitely growing streaming hash.

The default sweep uses lengths `16,64,256,1024,1440,4096,16384,16400,65536`,
depths `1,32`, a 50 ms warmup and three 250 ms trials per configuration.
After a successful sweep the module remains loaded with no active requests.
Unload it before another invocation. A failed insertion cleans up and logs
the failing configuration.

To pin the submitting thread, use `taskset` if installed:

```sh
taskset -c 0 insmod ghash_bench drivers=ghash-generic,ghash-neon \
    lengths=1440,16384 depths=1 duration_ms=1000 runs=5
rmmod ghash_bench
```

This is a **single submitting thread**. `depths=32` rotates through 32
independent request/buffer slots and permits up to 32 requests outstanding
if the selected algorithm actually completes asynchronously. A synchronous
GHASH implementation still executes one request at a time on that thread;
larger depth changes its working set, not CPU parallelism. The CSV `async`
count makes that distinction visible.

## MSS sizes and SG processing

To compare common Ethernet/PPPoE payload lengths:

```sh
insmod ghash_bench drivers=ghash-generic,ghash-neon \
    lengths=1408,1420,1428,1432,1440,1448,1452,1460,1472 \
    depths=1 duration_ms=1000 runs=5
rmmod ghash_bench
```

To hash one full record through MSS-sized scatterlist entries:

```sh
insmod ghash_bench drivers=ghash-generic,ghash-neon \
    lengths=16384 depths=1 sg_chunk=1440 duration_ms=1000 runs=5
rmmod ghash_bench
```

Compare with `sg_chunk=0`. The segmented case still computes **one digest
over the entire message**, using 12 SG entries for 16,384 bytes. It does
not hash each segment independently. Entries describe consecutive parts of
the same allocation; this isolates SG traversal and block-boundary costs,
without reproducing separately allocated TCP fragments or their cache state.
`sg_chunk` need not be a multiple of 16. Configurations needing more than
256 SG entries are rejected before allocation.

The shash-backed ahash frontend also divides SG entries at page boundaries.
A 64 KiB or larger message therefore does not establish that any one shash
call exercises a driver's 64 KiB internal loop limit. That needs a separate
direct-shash test with a sufficiently large update.

## GCM-formatted input

Raw mode (`aad_len=-1`) hashes the exact selected payload length, with a
partial final block zero-padded by GHASH. Setting `aad_len` to a nonnegative
value prepares this input outside timing:

```text
AAD || zero pad to 16 bytes || ciphertext || zero pad to 16 bytes
    || be64(AAD length in bits) || be64(ciphertext length in bits)
```

For a full-sized TLS 1.2 application record, use 13 bytes of AAD and
16,384 bytes of ciphertext:

```sh
insmod ghash_bench drivers=ghash-generic,ghash-neon \
    aad_len=13 lengths=16384 depths=1 duration_ms=1000 runs=5
rmmod ghash_bench
```

For a TLS 1.3 record carrying 16,384 application bytes and one inner
content-type byte, without additional padding:

```sh
insmod ghash_bench drivers=ghash-generic,ghash-neon \
    aad_len=5 lengths=16385 depths=1 duration_ms=1000 runs=5
rmmod ghash_bench
```

These hash 16,416 and 16,432 bytes respectively. Input bytes are reproducible
synthetic data, not captured TLS records. This mode measures the correct
GHASH work size through one `digest` call. It does **not** reproduce GCM's
separate init/update/finup calls, tag encryption/XOR, AES-CTR, authentication
checking or TCP/kTLS costs. GHASH always uses a 128-bit hash subkey even with
AES-192/256, so it needs no AES key-length sweep. The test derives one
reproducible subkey as `H = AES_K(0)` and reuses it for all comparisons.

## ARM64 NEON shuffle comparison

Install `kmod-crypto-ghash-neon-shuffle` and load `ghash_neon_shuffle.ko`.
This is a separate module with the Crypto API driver names
`ghash-neon-shuffle`, `ghash-neon-sched`, `ghash-neon-gpr`, `ghash-neon-2x`
and `ghash-neon-2x-pmull`, plus `ghash-neon-pmull`, `ghash-neon-orig-pmull`
and `ghash-neon-fused`, plus `ghash-neon-2x-reuse`, `ghash-neon-2x-load`
and `ghash-neon-4x`, plus (module release 12) `ghash-neon-8x` and
(module release 13) `ghash-neon-orig-pmull13`, plus (module release 14)
`ghash-neon-pmul` and `ghash-neon-umull`, plus (module release 15)
`ghash-neon-orig-pmull12`, plus (module release 16) `ghash-neon-4x-pmull12`
and (module release 17) `ghash-neon-4x-pmull13`.
They can be compared with the built-in `ghash-neon` without rebuilding or
replacing the kernel. Their priorities are below the stock implementations;
select the new drivers explicitly. There is no module
parameter to switch implementations.

```sh
modprobe ghash_neon_shuffle
taskset -c 0 insmod ghash_bench \
    drivers=ghash-neon-4x,ghash-neon-8x \
    lengths=16,64,112,128,144,192,256,1440,16384,16400,65536 depths=1 \
    duration_ms=1000 warmup_ms=100 runs=5
rmmod ghash_bench
```

Use benchmark release 2 to recognize built-in stock GHASH without requiring
`ghash-ce.ko`. Reverse the driver list on the next run to alternate the order.
The CSV labels each implementation independently. The
[standalone module README](../crypto-ghash-neon-shuffle/README.md) describes
block-boundary and SG checks, the shuffle rewrite, scheduling, GPR-load,
two/four/eight-block aggregation, byte-PMULL reduction, folded-key, key-table reuse and
key-load scheduling experiments, and validation. The
`ghash-neon-2x-load` keeps the reuse variant's arithmetic and table traffic,
staging part of the next table through six GPRs during the current multiply.
It passes build/static and device short/SG/GCM-boundary checks. The supplied
three-seed capture has 204 passing rows; bulk throughput improves 6.2--6.8%
over reuse in both orders and reaches 1.488306 Gbit/s at 64 KiB. QEMU TCG
also passes 720 benchmark rows with full crypto self-tests and KASAN, plus
2,880 separate direct-shash boundary/state comparisons through 131,089 bytes.
Those checks validate correctness; QEMU rates do not predict A53 throughput.
Release 11 adds `ghash-neon-4x`, which shares three reconstructions and one
reduction across four blocks, retaining scheduled key loads and the
`ghash-neon-2x-load` tail path. It uses 99 byte PMULLs per four blocks versus
102 for two pairs, but reads more key-table bytes. Build/static checks,
922 QEMU benchmark rows and 5,400 direct-shash comparisons through 131,121
bytes pass. The device comparison passes 180 rows and 111,185,351 timed
requests, with reference/final-slot and guard checks. Four-block aggregation
gains 19.3% at 1440 bytes and about 22.5% at 16384 through 65536 bytes in
both orders, reaching 1.823721 Gbit/s at 64 KiB. It is now the fastest measured
bulk variant. The capture covers raw single-entry input and one seed;
intermediate digests are reused, and complete GCM/kTLS is not measured.
Release 12 adds the eight-block comparison, sharing one reduction and three
reconstructions across eight blocks. It rereads input for each Karatsuba
part to avoid spills, and uses the unchanged four-block path for short
calls/tails. The device comparison passes 220 rows and 123,849,377 timed
requests, but 8x is 3.5--7.6% slower at 128--256 bytes and 0.8--0.9%
slower at 1440 bytes in both orders. The 16--64 KiB medians differ by
less than 0.07%; there is no useful bulk gain. Keep 4x as the preferred
implementation. The capture uses one seed and raw, contiguous input,
with reference/final-slot and guard checks and intermediate digests reused.
See the
[eight-block experiment](../crypto-ghash-neon-shuffle/README.md#eight-block-aggregation-experiment-release-12)
for the tradeoffs and commands.
Run the short/SG and GCM-boundary checks in the standalone module README
before the timed comparison above. Device comparisons show no measurable
gain for the shuffle-only variant, and a repeatable gain of about 0.7% for
the scheduled variant in both driver orders. GPR loading passes its contiguous
checks but shows no convincing gain over scheduled. Two-block aggregation
passes short/SG and contiguous checks and gains 18--20% over scheduled in
both driver orders. The pair's byte-PMULL reducer passes short/SG and
contiguous checks and gains another 1.6--1.8% in both orders. Single-block
PMULL reduction passes short/SG and contiguous checks with a 2.1--2.2%
gain over scheduled in both orders. The original-loop PMULL variant also
passes short/SG and contiguous checks, gaining 2.8--2.9% over stock for
1440 through 65536 bytes in both orders (1.2% at 16 bytes). Its bulk medians
are within 0.07% of the preceding scheduled-PMULL capture; that comparison
is across captures rather than a direct head-to-head trial. Benchmark
release 2 works unchanged with all eighteen comparison drivers. It accepts
at most eight selected names per insertion, so compare subsets of the
experimental and stock drivers. The fused variant passes short/SG and contiguous
checks, but is 1.8% slower at 16 bytes and 7.7--8.1% slower at 1440 through
65536 bytes than original-loop PMULL in both orders. Its shorter loop uses
more byte PMULLs and key loads; the timings do not isolate their individual
costs. See the standalone module README for the measurements and their
verification limits.

Module release 13 adds `ghash-neon-orig-pmull13` for a direct comparison
with the original-loop 15-instruction PMULL reducer. The new reducer uses
a masked constant to place the correction product directly in the high
two bytes, saving two instructions per block at the cost of two additional
setup instructions per call. See the
[13-instruction experiment](../crypto-ghash-neon-shuffle/README.md#thirteen-instruction-reducer-experiment-release-13)
for short-input checks and measurements in both driver orders. The A53
device comparison passes and gains approximately 0.7% at 1440 through
65536 bytes over the 15-instruction reducer.

Module release 14 adds `ghash-neon-pmul` and `ghash-neon-umull`. Their
complete little-endian loops have 152 and 235 instructions per block,
versus 133 for `ghash-neon-orig-pmull13`; neither prototype reduces the
instruction count. See the
[PMUL/UMULL experiment](../crypto-ghash-neon-shuffle/README.md#pmul-and-integer-umull-experiments-release-14)
for the arithmetic, validation and device measurement commands.
The A53 comparison passes its digest/guard checks but loses 17.5% and
62.5% throughput at 64 KiB relative to the 13-instruction reducer.

Module release 15 adds `ghash-neon-orig-pmull12`. One two-register `TBL`
replaces low-byte extraction followed by rotation, reducing the complete
loop from 133 to 132 instructions. Setup adds a 16-byte constant load and
two address instructions per call. See the
[12-instruction experiment](../crypto-ghash-neon-shuffle/README.md#twelve-instruction-reducer-experiment-release-15)
for checks and measurements against `orig-pmull13` in both driver orders.
The device comparison passes and gains about 1.4--1.5% at 1440 through
65536 bytes, with small regressions at 16/32 bytes.

Module release 16 adds `ghash-neon-4x-pmull12`. It uses the same reducer
in the four-block loop. Preparing two constants in registers after the
products finish avoids new per-group loads or spills, giving a net saving
of one instruction per four blocks. The key format, table-loading sequence
and short/tail path are unchanged. See the
[four-block reducer experiment](../crypto-ghash-neon-shuffle/README.md#four-block-twelve-instruction-reducer-experiment-release-16)
for boundary checks and measurements in both driver orders.
The device comparison passes 260 rows, gaining 0.26--0.34% at 16--64 KiB
in each order, with small short-input regressions.

Module release 17 adds `ghash-neon-4x-pmull13`, keeping the replicated
multiplier in a register throughout the loop. Its 13-instruction reducer
and one constant-preparation shift match `4x-pmull12`'s 14 per group,
while avoiding the additional shuffle-table load and reducing setup by
three instructions. See the
[four-block 13-instruction experiment](../crypto-ghash-neon-shuffle/README.md#four-block-thirteen-instruction-reducer-experiment-release-17)
for comparison commands against both preceding four-block variants.

The release-9 reuse variant alternates low/high/cross and cross/high/low
product order. It loads 240 key-table bytes for the first pair and 160 for
each subsequent pair, retaining 51 byte PMULLs and one reduction per pair.
It uses the existing two-block key setup/context and odd-tail path. Build,
static and device short/SG checks pass. Device comparisons gain about 1.0%
at 64 bytes and 4.8--5.5% at 1440 through 65536 bytes in both orders, reaching
1.395769 Gbit/s at 65536 bytes. The 16/32-byte cases show little change and
do not reuse tables between pairs. See the module README for the complete
table and verification limits.
Compare it against `ghash-neon-2x-pmull` using the commands above and the
standalone module README's short/fragmented and call-boundary checks.

## Output and verification

Rows in `dmesg` use this CSV schema:

```text
kind,format,requested,driver,length,input_length,depth,sg_nents,run,requests,payload_bytes,hashed_bytes,elapsed_ns,mbps,hash_mbps,ns_per_request,submit_cpu_ns,async,backlogs
```

- `mbps` counts selected payload bytes, in decimal Mbit/s; divide by 1000
  for Gbit/s. This matches the payload accounting of the CTR benchmarks.
- `hash_mbps` counts all 16-byte GHASH blocks, including final zero padding
  and, when enabled, AAD and the length block. `input_length` is the exact
  number of bytes submitted; `hashed_bytes` includes block rounding.
- `ns_per_request` is wall time divided by completed message count. With
  asynchronous requests in flight it is not individual request latency.
- `submit_cpu_ns` is the submitting task's user+system CPU time for the
  timed interval. It excludes preparation and verification, and is not
  system-wide CPU time; asynchronous worker/IRQ work may run elsewhere.
- `async` and `backlogs` count accepted `-EINPROGRESS`/`-EBUSY` requests and
  the `-EBUSY` subset respectively. Requests use `MAY_BACKLOG`, and all are
  drained before their memory is reused or freed.

An independent bit-serial GF(2^128) reference checks every slot's digest.
The reference first checks the 28-byte GHASH vector from `crypto/testmgr.h`.
Input bytes and surrounding source/digest guards are also checked after
preflight, warmup and each timed trial. Intermediate timed digests are
reused, so the benchmark does not claim to verify every timed operation.

Parameters are read-only after insertion. `timeout_ms` defaults to 10,000;
if a callback stalls the module reports it, then retains the request and
waits for completion before cleanup because the Crypto API has no general
request cancellation operation. There is no completion busy-wait loop.

This repeatedly hashes a bounded set of buffers and measures Crypto API
GHASH throughput with that cache working set. It is not a DRAM bandwidth
test. Compare it with the roughly 1.3 Gbit/s full-record AES-128 CTR result
to estimate whether GHASH leaves enough CPU capacity to feed the engine;
the combined kTLS rate still requires a device measurement.

Release 1 passes build-only checks against Linux 6.18.63 with GCC 14:
`ghash_bench.ko` compiles, passes modpost symbol resolution and links on
x86-64 and ARM64 with `W=1 KCFLAGS=-Werror`. The ARM64 `ghash-ce.ko`
dependency also compiles and links with these flags. The supporting ARM64
kernel enables skcipher support, which PonWrt normally supplies; its
intentional syscall/trap table overrides require a warning exception only
for those two kernel objects, not for either GHASH module.

No modules were loaded, and no runtime correctness checks or benchmarks
were run. These are Kbuild checks, not an OpenWrt package/image build or
validation of the device's exact kernel ABI.

Release 2 also passes x86-64 and ARM64 GCC 14 build-only checks with
`W=1 KCFLAGS=-Werror`. It adds the built-in-driver check and selects the
independent shuffle module instead of forcing the stock ARM64 GHASH
package. Its benchmark, input and verification behavior are otherwise
unchanged. Runtime comparison with the built-in driver remains a device
check; no local target code or benchmarks were executed for this release.
