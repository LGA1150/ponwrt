# Userspace ARM64 GHASH benchmark

`ghash-bench-userspace` installs `ghash-bench`, a standalone Linux AArch64
program for comparing the 77 implementations in
[`crypto-ghash-neon-shuffle`](../../kernel/crypto-ghash-neon-shuffle/README.md).
It compiles the same assembly and NEON-intrinsics C sources, without loading
a kernel module or calling the Crypto API. No root privileges are required.
The CPU must support ASIMD; the optional 64-bit polynomial-multiply extension
is not required. The names printed by `--list` identify compiled functions,
not drivers selected from `/proc/crypto`.

## Build

Select `ghash-bench-userspace` under Utilities in OpenWrt. The comparison
module's source directory must be present in the checkout, but its package
does not need to be selected or installed.

Alternatively, build directly from the repository root on an AArch64 host:

```sh
make -C package/utils/ghash-bench-userspace/src
```

Or cross-compile with the device's userspace toolchain:

```sh
make -C package/utils/ghash-bench-userspace/src \
    CC=aarch64-openwrt-linux-musl-gcc CFLAGS='-O2 -g'
```

The executable is `package/utils/ghash-bench-userspace/src/ghash-bench`.
Use `LDFLAGS=-static` if the toolchain supplies static libc. A standalone
copy of `src/` also needs the comparison package's `ghash-neon-*.S` and
`ghash-neon-intrinsics.c/.h`; pass their directory as `GHASH_SRC` to make.
Kernel build headers are not needed. As with other Linux userspace builds,
the toolchain must supply libc and Linux UAPI headers.

The direct build defaults to `-O2`; the OpenWrt package uses `TARGET_CFLAGS`.
Record the compiler and flags when comparing the intrinsics variant: those
flags affect generated C code, while the assembly loops remain unchanged.

## Correctness

Every invocation first checks fixed GHASH and RFC 8439 ChaCha20 known
answers, including register preservation around the selected functions.
The full selftest is:

```sh
ghash-bench -a all --selftest
```

It checks lengths 0 through 257, plus block, page and aggregation boundaries
up to 131201 bytes. Four key/payload patterns (zero, all ones and two seeded
ChaCha20 streams) exercise inputs at offsets 0, 1, 7 and 15, both in one call
and with continuing state across several calls. A protected page immediately
after the padded input catches reads past the last block. Input and digest
guards detect writes outside their bounds. All 77 variants give 776160
checks, in addition to the startup known answers.

The reference is independent, bit-serial GF(2^128) arithmetic. Partial
blocks are zero-padded once, before hashing. This is GHASH of the supplied
bytes, not complete GCM: it does not add AAD/ciphertext length fields or
encrypt the final tag. `--selftest` uses its fixed size/offset matrix rather
than `-s` and `-o`.

## Measure on the device

For the current single-block and four-block comparisons, run both orders
on the same CPU:

```sh
ghash-bench -c 0 \
    -a ghash-neon-schoolbook,ghash-neon-4x,ghash-neon-4x-pmull13,ghash-neon-4x-schoolbook \
    -s 16,32,64,1440,16384,16400,65536 -d 1000 -w 100 -r 5 \
    > ghash-userspace-forward.csv
ghash-bench -c 0 \
    -a ghash-neon-4x-schoolbook,ghash-neon-4x-pmull13,ghash-neon-4x,ghash-neon-schoolbook \
    -s 16,32,64,1440,16384,16400,65536 -d 1000 -w 100 -r 5 \
    > ghash-userspace-reverse.csv
```

`-s` accepts up to 64 lengths from 1 byte through 16 MiB. `-o 1` selects an
unaligned input; `-o` accepts 0 through 127. Use `--seed` for another
reproducible key/payload, `--list` for all names, and `--help` for options.

Preparation, allocation, key expansion, padding and verification occur
outside the timed windows. The benchmark reuses one input buffer and key,
resets the digest for each invocation, and checks the preflight digest,
the final digest of every warmup/trial, and input/output guards. It does
not retain or verify every intermediate timed digest. This measures repeated
hashing of a reused buffer, not streaming DRAM throughput.

The raw kernel assembly does not preserve AAPCS64's callee-saved SIMD
registers. A small assembly wrapper saves their required low 64-bit halves
once per timing batch, then calls the original functions. Timing includes
digest reset, calls, loop control, wrapper entry/exit and clock reads, but
no Crypto API dispatch or kernel SIMD entry/exit. `-b` controls calls per
batch (default 64); the actual batch is capped at 1 MiB of hashed data, or
one call for a larger input. Use `-b 1` to measure one wrapper entry/exit
per hash. A timed window can exceed its requested duration by one batch.

CSV columns include logical input length, padded hash length, actual batch,
requests, exact byte counts, wall time and thread CPU time. `gbps` counts
logical input bytes; `hash_gbps` counts the complete padded blocks processed.
Summary lines report min/median/max and the byte/time aggregate for each
variant/length. On the 1.2 GHz A53, `ns_per_request * 1.2` estimates elapsed
cycles per request, including the timed harness overhead and scheduling.

Release 2 also exposes `ghash-neon-4x-schoolbook-reuse` and
`ghash-neon-4x-schoolbook-load`. They use the same 592-byte key context and
single-block tail as `ghash-neon-4x-schoolbook`. The first alternates the
rotated-key traversal to retain its endpoint in registers; the second
additionally schedules scalar loads of the next keys during arithmetic.
Compare all three with the existing Karatsuba control:

```sh
ghash-bench -c 3 \
    -a ghash-neon-4x-schoolbook,ghash-neon-4x-schoolbook-reuse,ghash-neon-4x-schoolbook-load,ghash-neon-4x-pmull13 \
    -s 64,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with the algorithm and size orders reversed. The
[device results and validation](../../kernel/crypto-ghash-neon-shuffle/README.md#schoolbook-table-traversal-and-load-scheduling-release-22)
record the CPU3 comparison at 1.2 GHz. Default algorithm selection is unchanged.

To collect hardware counters for one variant:

```sh
perf stat -e cycles,instructions,task-clock -- \
    ghash-bench -c 0 -a ghash-neon-4x -s 65536 -d 2000 -w 200 -r 5
```

`perf stat` covers the entire process, including untimed preparation and
verification. Longer trials amortize that work; its whole-process counts
are not the exact cycles/instructions of the assembly loop. Repeat with
`ghash-neon-4x-schoolbook` under the same conditions. Perf access depends
on the device's usual permissions.

## QEMU correctness checks

A statically linked binary can run under user-mode TCG:

```sh
qemu-aarch64 -cpu cortex-a53 ./ghash-bench -a all --selftest
```

For a dynamic binary, also supply its matching libc/sysroot with QEMU's
`-L` option. QEMU timings do not represent Cortex-A53 throughput.

## Extra Karatsuba and scalar experiments (release 3)

Release 3 adds six variants shared with comparison-module release 23:

| Name suffix after `ghash-neon-4x-` | Change |
| --- | --- |
| `k32` | Additional 32-bit Karatsuba split inside each 64-bit product |
| `k32-load` | Schedule scalar key loads alongside those SIMD products |
| `k32-sched` | Also interleave independent reconstruction chains |
| `k32-tbl` | Also replace reconstruction shifts with fixed shuffles |
| `gpr-reduce` | Existing four-block multiplication with scalar reduction |
| `stage` | Scalar input preparation and stores interleaved with multiplication |

The K32 variants use a 688-byte key context and the existing two-block tail
routine. The other two retain the 736-byte four-block context. Input length,
offset, verification and timing behavior are unchanged. No PMULL CPU extension
is needed: these implementations use the baseline ASIMD byte instructions.

Run the new variants' full correctness matrix, then compare both orders on
CPU3, keeping the governor and frequency fixed:

```sh
ghash-bench -c 3 \
    -a ghash-neon-4x-k32,ghash-neon-4x-k32-load,ghash-neon-4x-k32-sched,ghash-neon-4x-k32-tbl,ghash-neon-4x-gpr-reduce,ghash-neon-4x-stage \
    --selftest
ghash-bench -c 3 \
    -a ghash-neon-4x-pmull13,ghash-neon-4x-pmull12,ghash-neon-4x-k32,ghash-neon-4x-k32-load,ghash-neon-4x-k32-sched,ghash-neon-4x-k32-tbl,ghash-neon-4x-gpr-reduce,ghash-neon-4x-stage \
    -s 64,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5 \
    > next-forward.csv
ghash-bench -c 3 \
    -a ghash-neon-4x-stage,ghash-neon-4x-gpr-reduce,ghash-neon-4x-k32-tbl,ghash-neon-4x-k32-sched,ghash-neon-4x-k32-load,ghash-neon-4x-k32,ghash-neon-4x-pmull12,ghash-neon-4x-pmull13 \
    -s 65536,16400,16384,1440,256,128,64 -d 300 -w 100 -r 5 \
    > next-reverse.csv
```

The recorded device run used separate processes for each algorithm/length,
with this same matrix and both orders. Its 64-KiB medians are **1.833903 Gbps**
for PMULL12 and **1.962749 Gbps** for `k32-tbl`, a **7.03%** improvement.
Scalar reduction and input staging remain comparison targets, with no
substantial bulk gain. See the [algorithm, full seven-length table and PMU results](../../kernel/crypto-ghash-neon-shuffle/README.md#extra-karatsuba-scalar-reduction-and-stores-release-23).

On this A53, collect a non-multiplexed userspace-only counter group with:

```sh
taskset -c 3 perf stat -I 1000 -x, -o next-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-4x-k32-tbl \
    -s 65536 -d 3000 -w 100 -r 1 > next-pmu.csv
```

Intervals ending near two and three seconds exclude startup and the final
partial interval. Check that each event was scheduled for the whole interval.
The raw events are A53 SIMD, AGU and load interlocks respectively; their
encodings and meanings are CPU-specific, and they do not form a complete
partition of stalled cycles. Repeat for the controls in both orders.

## Deeper Karatsuba splits (release 4)

Release 4 adds `ghash-neon-4x-k16`, `ghash-neon-4x-k16-load`,
`ghash-neon-4x-k8`, and `ghash-neon-4x-k8-load`, shared with comparison-module
release 24. They extend the 32-bit Karatsuba implementation to 16-bit and
8-bit operands. The `-load` variants overlap scalar key loads with SIMD
arithmetic; the other variants use vector key loads. Each uses a 688-byte
context with the existing two-block fallback. The selection capacity is
raised to 64 to accommodate all 34 implementations.

```sh
ghash-bench -c 3 \
    -a ghash-neon-4x-k16,ghash-neon-4x-k16-load,ghash-neon-4x-k8,ghash-neon-4x-k8-load \
    --selftest
ghash-bench -c 3 \
    -a ghash-neon-4x-k32-tbl,ghash-neon-4x-k16,ghash-neon-4x-k16-load,ghash-neon-4x-k8,ghash-neon-4x-k8-load \
    -s 64,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5 \
    > deep-forward.csv
ghash-bench -c 3 \
    -a ghash-neon-4x-k8-load,ghash-neon-4x-k8,ghash-neon-4x-k16-load,ghash-neon-4x-k16,ghash-neon-4x-k32-tbl \
    -s 65536,16400,16384,1440,256,128,64 -d 300 -w 100 -r 5 \
    > deep-reverse.csv
```

Use the same interval-based perf command above to compare the deeper splits
with `ghash-neon-4x-k32-tbl`. The [implementation and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#deeper-karatsuba-splits-release-24)
describe the tradeoff between fewer byte multiplies and more reconstruction.

On the Cortex-A53 at 1.2 GHz, the pooled 64-KiB medians are 1.964473 Gbit/s
for `k32-tbl`, 1.718960 for `k16`, 1.777987 for `k16-load`, 1.294064 for
`k8`, and 1.328383 for `k8-load`. Keep `k32-tbl`; these deeper splits save
PMULL instructions but take longer. Each new implementation passed 10,080
full selftest checks on both the device and QEMU TCG.

## Single-block K32 (release 5)

Release 5 adds `ghash-neon-k32` and `ghash-neon-k32-packed`, shared with
comparison-module release 25. Both perform one multiply and reduction per
16-byte input block, with 32-bit Karatsuba splits and key operands retained
in registers. The packed variant fills the otherwise unused lanes in the
ninth 32-bit product. Both use the existing 16-byte single-block key layout.

```sh
ghash-bench -c 3 -a ghash-neon-k32,ghash-neon-k32-packed --selftest
ghash-bench -c 3 \
    -a ghash-neon-orig-pmull13,ghash-neon-orig-pmull12,ghash-neon-schoolbook,ghash-neon-k32,ghash-neon-k32-packed \
    -s 16,32,64,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5 \
    > single-forward.csv
ghash-bench -c 3 \
    -a ghash-neon-k32-packed,ghash-neon-k32,ghash-neon-schoolbook,ghash-neon-orig-pmull12,ghash-neon-orig-pmull13 \
    -s 65536,16400,16384,1440,256,128,64,32,16 -d 300 -w 100 -r 5 \
    > single-reverse.csv
```

The [implementation and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#single-block-k32-release-25)
compare instruction counts, throughput and PMU events against the existing
single-block PMULL13, PMULL12 and schoolbook implementations.

At 64 KiB on the Cortex-A53 at 1.2 GHz, ten-trial pooled medians are
1.123105 Gbit/s for PMULL13, 1.140144 for PMULL12, 1.175232 for schoolbook,
1.222356 for K32 and 1.193237 for K32-packed. K32 improves on PMULL13 by
8.84% and is the faster of the two new variants. Both pass 20,160 combined
full selftest comparisons on the device and another 20,160 under QEMU TCG.

Release 6 uses the comparison module's release-26 mask-load addressing:
the single-block K32 functions load the fifth mask at the fixed offset
`[x4, #64]`, with no post-indexed writeback or extra address calculation.
Their hashing loops are unchanged. See the [addressing change and validation](../../kernel/crypto-ghash-neon-shuffle/README.md#single-block-mask-load-addressing-release-26).

## Direct SIMD key loads (release 7)

Release 7 adds three variants of `ghash-neon-4x-k32-tbl` that load keys
directly into SIMD registers. Their multiplication, table permutations,
key context and short-input fallback are shared with the scalar-load
implementation:

| Name suffix after `ghash-neon-4x-k32-tbl` | Key-load schedule |
| --- | --- |
| No suffix | Six scalar loads overlap SIMD work, followed by `FMOV`/`INS` |
| `-simd` | One three-vector `LD1` immediately before each product group |
| `-simd-pipe` | The next three-vector `LD1` follows the current group's last multiply, before its final XORs |
| `-simd-ldr` | Each next `LDR Q` follows that key vector's last use |

```sh
ghash-bench -c 3 \
    -a ghash-neon-4x-k32-tbl,ghash-neon-4x-k32-tbl-simd,ghash-neon-4x-k32-tbl-simd-pipe,ghash-neon-4x-k32-tbl-simd-ldr \
    --selftest
taskset -c 3 ./ghash-bench -c 3 \
    -a ghash-neon-4x-k32-tbl,ghash-neon-4x-k32-tbl-simd,ghash-neon-4x-k32-tbl-simd-pipe,ghash-neon-4x-k32-tbl-simd-ldr \
    -s 64,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5 \
    > simd-forward.csv
taskset -c 3 ./ghash-bench -c 3 \
    -a ghash-neon-4x-k32-tbl-simd-ldr,ghash-neon-4x-k32-tbl-simd-pipe,ghash-neon-4x-k32-tbl-simd,ghash-neon-4x-k32-tbl \
    -s 65536,16400,16384,1440,256,128,64 -d 300 -w 100 -r 5 \
    > simd-reverse.csv
```

For each driver, collect grouped PMU events in separate 64-KiB runs:

```sh
taskset -c 3 perf stat -I 1000 -x, -o simd-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-4x-k32-tbl-simd-ldr \
    -s 65536 -d 3000 -w 100 -r 1 > simd-pmu.csv
```

The [comparison and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#direct-simd-key-loads-for-k32-tbl-release-27)
include all four schedules, instruction counts, throughput and PMU events.

## Eight-block K32 aggregation (release 8)

Release 8 adds `ghash-neon-8x-k32-tbl`, which keeps the current scalar key
loads and combines two four-block groups before the outer merge and field
reduction. It reads each input block once and uses no scratch stores or
spills. Its 1120-byte context retains the full 4-block context for short
inputs and tails.

```sh
ghash-bench -c 3 -a ghash-neon-4x-k32-tbl,ghash-neon-8x-k32-tbl --selftest
taskset -c 3 ./ghash-bench -c 3 \
    -a ghash-neon-4x-k32-tbl,ghash-neon-8x-k32-tbl \
    -s 64,112,128,144,256,1440,16384,16400,65536 -d 300 -w 100 -r 5 \
    > eight-forward.csv
taskset -c 3 ./ghash-bench -c 3 \
    -a ghash-neon-8x-k32-tbl,ghash-neon-4x-k32-tbl \
    -s 65536,16400,16384,1440,256,144,128,112,64 -d 300 -w 100 -r 5 \
    > eight-reverse.csv
```

On Cortex-A53 CPU3 at 1.2 GHz, ten-trial pooled 64-KiB medians are
1.962846 Gbit/s for the 4-block version and 2.026650 for the 8-block
version, a 3.25% gain. At 16 KiB the gain is 3.19%, to 2.035803 Gbit/s.
Every 8-block trial wins at every tested size of 128 bytes or more.
The [implementation and validation](../../kernel/crypto-ghash-neon-shuffle/README.md#eight-block-k32-aggregation-release-28)
include all nine sizes, loop instruction counts and grouped PMU results.

## Scratch key stores (release 9)

Release 9 adds grouped and scheduled SIMD reloads from scalar-store scratch
space, for both four- and eight-block K32 aggregation. The names are
`ghash-neon-{4x,8x}-k32-stp` and `ghash-neon-{4x,8x}-k32-stp-pipe`.
The 48-byte stack buffer is private to each call and cleared on return.

```sh
algorithms=ghash-neon-4x-k32-tbl,ghash-neon-4x-k32-stp,ghash-neon-4x-k32-stp-pipe,ghash-neon-8x-k32-tbl,ghash-neon-8x-k32-stp,ghash-neon-8x-k32-stp-pipe
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 64,112,128,144,256,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with the algorithm and size lists reversed to check order effects.
The [implementation and measurements](../../kernel/crypto-ghash-neon-shuffle/README.md#scratch-key-stores-release-29)
compare against the existing scalar-register transfers and include IPC and
interlock counters.

## Shared K32 reconstruction and lane scheduling (release 10)

Release 10 adds five `ghash-neon-8x-k32-merge` implementations that share
inner product reconstruction across all eight blocks. The unsuffixed
version retains the scalar key-load and grouped lane-transfer schedule.
Suffixes `-simd`, `-ldr`, `-early` and `-lanes` compare grouped SIMD loads,
individually scheduled SIMD loads, early adjacent scalar-to-SIMD lane
writes and lane writes separated by independent arithmetic. The separate
`ghash-neon-8x-k32-early` and `ghash-neon-8x-k32-lanes` controls apply the
last two schedules to the previous reconstruction loop.

```sh
algorithms=ghash-neon-8x-k32-tbl,ghash-neon-8x-k32-merge,ghash-neon-8x-k32-merge-simd,ghash-neon-8x-k32-merge-ldr,ghash-neon-8x-k32-merge-early,ghash-neon-8x-k32-early,ghash-neon-8x-k32-merge-lanes,ghash-neon-8x-k32-lanes
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 64,112,128,144,256,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with algorithm and size lists reversed. For each selected algorithm,
collect IPC and interlocks separately from the timing comparison:

```sh
taskset -c 3 perf stat -I 1000 -x, -o merge-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-8x-k32-merge \
    -s 65536 -d 3000 -w 100 -r 1 > merge-pmu.csv
```

The [implementation and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#shared-k32-reconstruction-and-lane-scheduling-release-30)
compare all eight targets, with loop counts, nine-size throughput tables,
IPC, interlocks and correctness coverage.

On Cortex-A53 CPU3 at 1.2 GHz, shared reconstruction with the original
scalar schedule reaches 2.192719 Gbit/s at 64 KiB, an 8.18% gain over the
previous eight-block loop. It is the fastest of these eight targets at
every tested size from 128 bytes upward. All eight pass the 80,640-check
full selftest both natively and under user-mode QEMU TCG.

## Fewer Karatsuba splits (release 11)

Release 11 compares the previous eight-block winner with direct products
at the 128-bit split (`no128`), the 64-bit split (`no64`), or both
(`no128-no64`). Two `-early` variants prepare the swapped 32-bit input
before the low/high products to separate shuffle dependencies. All names
start with `ghash-neon-8x-k32-`. Short inputs and tails keep the existing
four-block fallback, including its Karatsuba operations.

```sh
algorithms=ghash-neon-8x-k32-merge,ghash-neon-8x-k32-no128,ghash-neon-8x-k32-no64,ghash-neon-8x-k32-no128-no64,ghash-neon-8x-k32-no64-early,ghash-neon-8x-k32-no128-no64-early
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 64,112,128,144,256,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with algorithm and size lists reversed. Collect grouped PMU events
for each target in separate runs:

```sh
taskset -c 3 perf stat -I 1000 -x, -o splits-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-8x-k32-no64-early \
    -s 65536 -d 3000 -w 100 -r 1 > splits-pmu.csv
```

The [implementation and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#fewer-karatsuba-splits-release-31)
include operation counts, all nine sizes, IPC, interlocks and correctness
coverage for the five new comparisons.

On Cortex-A53 CPU3 at 1.2 GHz, keeping both splits remains fastest at all
tested sizes from 128 bytes upward. At 64 KiB the control reaches
2.193252 Gbit/s; the best reduced-Karatsuba schedule reaches 2.061374,
6.01% lower. All six pass the 60,480-check selftest both natively and under
user-mode QEMU TCG.

## Sixteen-block aggregation (release 12)

Release 12 adds `ghash-neon-16x-k32`, sharing one field reduction across
two eight-block groups without spills, and `ghash-neon-16x-k32-merge`,
sharing the inner reconstruction too. The latter uses 128 bytes of stack
scratch for input intermediates. Both retain the existing eight-block
fallback and use a 1984-byte expanded key context.

```sh
algorithms=ghash-neon-8x-k32-merge,ghash-neon-16x-k32,ghash-neon-16x-k32-merge
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 64,128,240,256,272,512,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with algorithm and size lists reversed. Measure grouped PMU events
separately for each target:

```sh
taskset -c 3 perf stat -I 1000 -x, -o agg16-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-16x-k32-merge \
    -s 65536 -d 3000 -w 100 -r 1 > agg16-pmu.csv
```

On Cortex-A53 CPU3 at 1.2 GHz, fully shared 16x reaches 2.286045 Gbit/s
at 64 KiB, versus 2.191650 for 8x and 2.232995 for 16x without spills.
The 4.31% bulk gain offsets the extra scratch traffic in this test; short
calls show a modest regression. All three pass the 30,240-check selftest
both natively and under user-mode QEMU TCG. The
[implementation and device results](../../kernel/crypto-ghash-neon-shuffle/README.md#sixteen-block-k32-aggregation-release-32)
include all ten sizes, instruction counts, IPC, interlocks and validation.

## Four-block K32 operand reuse (release 13)

Release 13 adds five four-block variants that compare fewer input
shuffles, reuse of inner Karatsuba cross operands and earlier scheduling.
They retain the old 688-byte key context and two-block fallback, with no
stack scratch or spills. The combined versions execute 360 instructions
per 64-byte little-endian iteration instead of 367.

```sh
algorithms=ghash-neon-4x-k32-tbl,ghash-neon-4x-k32-input,ghash-neon-4x-k32-cross,ghash-neon-4x-k32-tight,ghash-neon-4x-k32-tight-early,ghash-neon-4x-k32-pipe
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 16,32,64,80,112,128,256,1440,16384,16400,65536 -d 300 -w 100 -r 5
```

Repeat with the algorithm and size lists reversed. Measure grouped PMU
events separately for each target:

```sh
taskset -c 3 perf stat -I 1000 -x, -o fourp-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-4x-k32-pipe \
    -s 65536 -d 3000 -w 100 -r 1 > fourp-pmu.csv
```

On Cortex-A53 CPU3 at 1.2 GHz, the fastest bulk result is
`ghash-neon-4x-k32-tight-early` at 2.023391 Gbit/s for 64 KiB, versus
1.964431 for `ghash-neon-4x-k32-tbl`
(3.00% gain). All six targets pass 60,480 selftest checks
both natively and under user-mode QEMU TCG. See the
[full eleven-size comparison, PMU counters and kernel validation](../../kernel/crypto-ghash-neon-shuffle/README.md#four-block-k32-operand-reuse-and-scheduling-release-33).
Sources, executable and captures use the `fourp-` prefix under
`/tmp/ghash-test`; the executable is `fourp-final`.

## Compact-key single-block comparison (release 14)

Release 14 adds nine serial K32 variants using instruction scheduling,
two- or four-iteration unrolling, scalar input preloads, direct reduction
and a rescheduled packed ninth product. All have a 16-byte persistent
key and no assembly stack accesses; they do not aggregate key powers.
The `-a all` selection now has room for all 72 implementations.

```sh
algorithms=ghash-neon-orig-pmull13,ghash-neon-k32,ghash-neon-k32-packed,ghash-neon-k32-sched,ghash-neon-k32-u2,ghash-neon-k32-u4,ghash-neon-k32-u2-load,ghash-neon-k32-u4-load,ghash-neon-k32-direct,ghash-neon-k32-direct-u4-load,ghash-neon-k32-packed-sched,ghash-neon-k32-direct-stream
./ghash-bench -c 3 -a "$algorithms" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algorithms" \
    -s 16,32,64,128,1440,16384,65536 -d 300 -w 100 -r 5
```

Repeat with the algorithm and size lists reversed. Measure grouped PMU
events separately for each target:

```sh
taskset -c 3 perf stat -I 1000 -x, -o singleopt-pmu.perf \
    -e '{cycles:u,instructions:u,re6:u,re5:u,re7:u}' -- \
    ./ghash-bench -c 3 -a ghash-neon-k32-direct-stream \
    -s 65536 -d 3000 -w 100 -r 1 > singleopt-pmu.csv
```

On Cortex-A53 CPU3 at 1.2 GHz, `ghash-neon-k32-direct-stream`
reaches 1.348656 Gbit/s at 64 KiB versus 1.221927 for
`ghash-neon-k32` (10.37% faster). Its IPC is 1.0197 versus 0.9102.
`ghash-neon-k32-direct` uses a smaller 556-byte assembly function and
wins the 16-byte measurement. All twelve targets pass 120,960
native and 120,960 user-mode QEMU TCG selftest checks.

See the [full comparison, code sizes and kernel validation](../../kernel/crypto-ghash-neon-shuffle/README.md#compact-key-single-block-scheduling-and-unrolling-release-34).
Sources, executable and captures use the `singleopt-` prefix under
`/tmp/ghash-test`; the executable is `singleopt-final`.

## Four-block aggregation with an 80-byte key (release 15)

`ghash-neon-4x-k32-compact` shares the kernel implementation's 80-byte
layout: raw H followed by four reflected powers. It constructs 432 bytes
of rotated operands on the stack once per assembly call of four or more
blocks, reuses them for the four-block loop, then clears the temporary
storage. Short calls and remaining blocks use `k32-direct-stream`.
The benchmark includes this per-call setup and clearing in its timings;
key setup remains excluded.

Compare the compact key with the 688-byte expanded-key control and the
16-byte single-block control on Cortex-A53 CPU3:

```sh
cd /tmp/ghash-test
algs=ghash-neon-4x-k32-tight-early,ghash-neon-4x-k32-compact,ghash-neon-k32-direct-stream
taskset -c 3 ./ghash-bench -c 3 -a "$algs" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algs" \
    -s 16,32,64,80,96,128,192,256,512,1440,16384,65536 -d 300 -w 100 -r 5
```

Reverse algorithm and length order for the second five-trial pass. The
recorded experiment performs each algorithm/size pair in a separate
invocation. Sources, captures and exact scripts use the `compact4-`
prefix under `/tmp/ghash-test`; the measured executable is `compact4-final`.
The [kernel comparison notes](../../kernel/crypto-ghash-neon-shuffle/README.md#four-block-aggregation-with-an-80-byte-key-release-35)
include the complete table, PMU counters and validation.

The harness's key union is sized for every selectable implementation.
The 80-byte figure is the actual kernel transform context size; these
hot-key measurements do not measure cache savings across many keys.

## Smaller stack for four-block aggregation (release 16)

Four new variants keep the 80-byte compact key while reducing temporary
stack use. `4x-k32-stack288` stores two operand rotations; `stack144`
stores only unrotated operands. `regs32` keeps nine operand vectors in
GPRs and saves four callee-saved registers in 32 stack bytes. `regs0`
keeps six operand vectors in caller-saved GPRs, generates inner cross
terms during the loop, and has no stack accesses in the GHASH routine.
All names below have the `ghash-neon-` prefix.

```sh
cd /tmp/ghash-test
algs=ghash-neon-4x-k32-compact,ghash-neon-4x-k32-stack288,ghash-neon-4x-k32-stack144,ghash-neon-4x-k32-regs32,ghash-neon-4x-k32-regs0
taskset -c 3 ./ghash-bench -c 3 -a "$algs" --selftest
taskset -c 3 ./ghash-bench -c 3 -a "$algs" \
    -s 16,32,64,80,96,128,192,256,512,1440,16384,65536 -d 300 -w 100 -r 5
```

Reverse algorithm and length order for the second five-trial pass. The
recorded run uses a separate invocation for each algorithm/size pair.
Entry setup, stack-table clearing and serial tails remain timed;
key setup and correctness checks are outside the timed windows.

Sources and captures use the `stack4-` prefix under `/tmp/ghash-test`;
the executable is `stack4-final`. The [kernel comparison notes](../../kernel/crypto-ghash-neon-shuffle/README.md#smaller-stack-for-four-block-aggregation-release-36)
contain the complete results and validation. Stack sizes refer to the
raw GHASH assembly routine; the userspace ABI wrapper and kernel SIMD
entry code have their own unchanged overhead.
