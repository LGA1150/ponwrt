# Standalone ARM64 NEON GHASH comparison drivers

`kmod-crypto-ghash-neon-shuffle` supplies `ghash_neon_shuffle.ko` for a
matching Linux 6.18 kernel. Release 16 registers algorithm `ghash` under seventeen
unique Crypto API driver names:

- **`ghash-neon-shuffle`** retains the release-1 shuffle implementation.
- **`ghash-neon-sched`** adds instruction scheduling and balanced reduction.
- **`ghash-neon-gpr`** loads the next block through two general-purpose registers.
- **`ghash-neon-2x`** aggregates two blocks before reduction.
- **`ghash-neon-2x-pmull`** uses byte PMULL for that pair's final reduction.
- **`ghash-neon-pmull`** uses byte-PMULL reduction after every single block.
- **`ghash-neon-orig-pmull`** applies it to the original stock loop.
- **`ghash-neon-fused`** folds part of reduction into a precomputed hash key.
- **`ghash-neon-2x-reuse`** reuses the last key table between PMULL block pairs.
- **`ghash-neon-2x-load`** overlaps the remaining key loads with multiplication.
- **`ghash-neon-4x`** accumulates four blocks before reconstruction and reduction.
- **`ghash-neon-8x`** accumulates eight blocks, rereading input to avoid spills.
- **`ghash-neon-orig-pmull13`** shortens the original-loop PMULL reducer to 13 instructions.
- **`ghash-neon-pmul`** uses truncated byte polynomial products and bit reversal.
- **`ghash-neon-umull`** uses integer widening products with the generic holes method.
- **`ghash-neon-orig-pmull12`** combines extraction and rotation in a 12-instruction reducer.
- **`ghash-neon-4x-pmull12`** applies that reducer to four-block aggregation.

Each name always selects its own implementation; there is no switching parameter.

The module coexists with a built-in or modular `ghash-neon`. It does not
replace `ghash-ce`, modify the base kernel, register AES-GCM algorithms,
or depend on the stock GHASH implementation. It depends on the Crypto API
hash framework and, from release 4, `gf128mul` for calculating powers of H
once at key setup. It requires ARM64 ASIMD, but does not require
the optional 64-bit PMULL extension. It can also run on CPUs with that
extension, although the stock Linux 6.18 driver does not register standalone
`ghash-neon` on those CPUs.

Priorities 99 (shuffle), 98 (sched), 97 (gpr), 96 (2x), 95 (2x-pmull),
94 (pmull), 93 (orig-pmull), 92 (fused), 91 (2x-reuse), 90 (2x-load),
89 (4x), 88 (8x), 87 (orig-pmull13), 86 (pmul), 85 (umull),
84 (orig-pmull12) and 83 (4x-pmull12) are below stock
`ghash-neon` (150) and `ghash-generic` (100), so
stock implementations win selection by `ghash`. Select the experimental drivers explicitly.
Crypto API module aliases cover only these seventeen unique names. The package has no
autoload or conflict entry and does not change `CONFIG_CRYPTO_GHASH_ARM64_CE`.

## Build and load

Select this package under Cryptographic API modules, or select
`kmod-ghash-bench`, which includes it as an ARM64 dependency from benchmark
release 2. Build the packages against the prepared kernel for the device
and install them. No change from `CONFIG_CRYPTO_GHASH_ARM64_CE=y` to `m` is
needed, and the built-in driver remains available throughout.

The sources under `src/` also support a direct external-module build:

```sh
make -C /path/to/prepared/linux-build M="$PWD/src" \
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
```

Run this from the package directory using a matching kernel build and
toolchain. After installation on the device:

```sh
modprobe ghash_neon_shuffle
```

Alternatively, load the built module with `insmod /path/to/ghash_neon_shuffle.ko`
when its `crypto_hash` and `gf128mul` dependencies are already available.
It does not need the EIP93 device, and EIP93/kTLS may remain loaded.

## Compare with the built-in implementation

Use `ghash_bench` release 2, which recognizes an already registered
`ghash-neon` before attempting to load the stock module. It accepts up to
eight drivers per insertion; select a comparison group, for example:

```sh
taskset -c 0 insmod ghash_bench \
    drivers=ghash-neon,ghash-neon-orig-pmull,ghash-neon-pmull,ghash-neon-2x-pmull,ghash-neon-fused \
    lengths=1440,16384,16400,65536 depths=1 \
    duration_ms=1000 warmup_ms=100 runs=5
rmmod ghash_bench
```

Repeat with
`drivers=ghash-neon-fused,ghash-neon-2x-pmull,ghash-neon-pmull,ghash-neon-orig-pmull,ghash-neon`
to reverse the order.
The CSV records the exact driver name, so no external parameter label is
needed. To isolate aggregation, use `drivers=ghash-neon-sched,ghash-neon-2x`
and then reverse those two names. The scheduled implementation is the control.

Before measuring throughput, exercise partial and full block boundaries
with the benchmark's independent reference and guard checks:

```sh
insmod ghash_bench drivers=ghash-neon-sched,ghash-neon-2x \
    lengths=1,2,3,15,16,17,31,32,33,47,48,49,63,64,65,1440,16384,16400,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
```

Repeat with different seeds and with `lengths=4097 sg_chunk=17` and
`lengths=4097 sg_chunk=47` to exercise partial-block buffering across SG
entries, one-block calls, pairs and odd tails. The
[benchmark README](../ghash-bench/README.md#gcm-formatted-input) also describes
TLS 1.2/1.3 GCM-formatted inputs. Unload the benchmark before unloading
`ghash_neon_shuffle`; active Crypto API transforms retain the module.

## Implementation and validation

The C glue and assembly are derived from Linux 6.18's ARM64 GHASH driver,
retaining its key reflection, digest format, block-only shash buffering,
zero-padded final block, and 64 KiB limit per kernel SIMD section. The
assembly entry points have unique names and are linked into this module;
they are not imported from the built-in driver. The stock and experimental
implementations therefore have independent algorithm and module lifetimes.

Four fixed two-register `TBL` permutations replace four `ZIP`/`EXT` pairs
in each polynomial product. Reduction uses `ZIP1`/`ZIP2` in place of four
lane inserts. Four extra vector registers hold the constant indices loaded
once per assembly invocation. The little-endian loop has 127 instructions
instead of the stock loop's 139, retaining 24 byte-lane polynomial multiplies.
There are no new input loads, spills or data-dependent memory accesses in
that loop. The built-in driver supplies the unmodified comparison baseline.
The shuffle loop's machine instructions are unchanged in releases 2 through 7.

Fewer instructions do not establish a speedup: two-register `TBL` costs and
the extra setup need measurements on the device. The module is experimental.

## Scheduling experiment (release 2)

`ghash-neon-sched` retains the same three Karatsuba products, 24 byte-lane
polynomial multiplies per block, and constant shuffle table. It changes
the instruction dependencies and placement:

- Start each multiplication group with an unrotated operand, increasing
  the gap before consuming the input rotations. Move remaining independent
  multiplies between XORs and the unzip operations that consume them.
- Express reduction as independent shifts and balanced XOR trees. In
  particular, `(L ^ (L >> 1)) >> 1` becomes `(L >> 1) ^ (L >> 2)` within
  each 64-bit lane. The reduced field element remains identical.
- Load the first input block before key preparation, and the next block
  while processing the current block. A remaining-block check skips this
  load on the final iteration. It reads exactly the full blocks supplied
  by the shash frontend, including for fragmented input; it never loads
  across the end of the supplied span.

The loop has 128 instructions on little-endian ARM64, one more than the
shuffle control because of the preload guard. It uses the previously free
`v4` for the next input block and introduces no spills. All C wrappers
inline to direct assembly calls, avoiding indirect dispatch overhead.
All variants keep the same 64 KiB limit per SIMD section and frontend
buffering of partial final blocks.

Cortex-A53 supports partial dual issue. Its NEON datapath and instruction
dependencies still constrain which operations can overlap; two independent
128-bit NEON instructions do not imply two operations per cycle. The
experiment aims to reduce dependency stalls. Its 128-bit vector preload
separates the load from its use; that alone does not establish dual issue.
See the
[Arm explanation of A53 issue limits](https://community.arm.com/forums/f/architectures-and-processors-forum/56101/typical-instruction-per-cycle-in-a53-cpu).
The device results below measure its benefit without attributing it to
particular instruction pairs.

## GPR input-load experiment (release 3)

`ghash-neon-gpr` keeps the scheduled variant's polynomial products and
balanced reduction. It replaces each 128-bit input vector load with two
64-bit `LDR` instructions into `x6` and `x7`. On little-endian systems,
scalar `REV` instructions reverse the bytes in each half, replacing the
vector `REV64`. `FMOV d4, x6` and `MOV v4.d[1], x7` assemble the block in
the free `NEXT` vector register.

The first block loads before key preparation. For subsequent blocks,
the two loads, scalar reversals and pointer update are interleaved with
the current block's reduction. Independent vector work separates the
loads from their register transfers, which also have vector instructions
between them. The new loop directly consumes the already byte-swapped
`NEXT` register. A remaining-block guard takes a separate copy of the
reduction on the final iteration, without loading another block. Each
invocation reads exactly its supplied full blocks, including one-block
calls and SG spans. The frontend still buffers and zero-pads partial
blocks as before.

This follows the GPR-load scheduling experiment described in
[Coding NEON kernels for the Cortex-A53](https://destevez.net/2025/02/coding-neon-kernels-for-the-cortex-a53/).
The nonfinal loop has 133 instructions on little-endian ARM64 versus 128
for `ghash-neon-sched`, and still has 24 byte polynomial multiplies.
Extra scalar instructions and register-transfer costs may outweigh any
load overlap; throughput must be measured. No PMULL/TBL latency or
dual-issue pairing is assumed from the article's floating-point examples.
Both existing assembly functions retain their release-2 instruction
encodings, so their performance comparisons remain useful controls.

After installing release 3, unload `ghash_bench` if it is still loaded,
then reload the comparison module before testing:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
```

Use `ghash-neon-gpr` in the block-boundary/SG checks above first.
For CPU-pinned measurements:

```sh
for order in ghash-neon-sched,ghash-neon-gpr ghash-neon-gpr,ghash-neon-sched; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Benchmark release 2 works unchanged; only the comparison module needs
rebuilding. The GPR driver remains below stock priorities and is selected
explicitly by name.

## Two-block aggregation experiment (release 4)

`ghash-neon-2x` combines two consecutive GHASH updates:

```text
Y' = (Y XOR X1) * H^2 XOR X2 * H
```

Key setup computes H squared with the kernel's `gf128mul_lle()`, then
reflects H and H squared into the representation used by the existing
assembly. Three tables hold corresponding low, high and XORed halves of
the two powers, with their four byte rotations. These occupy 240 bytes
following the original 16-byte H prefix in this variant's context. The
other variants retain their 16-byte key contexts. The package selects
`kmod-crypto-gf128`; it is already supplied with the benchmark's generic
GHASH dependency. The generic field-multiplication helper runs only at
key setup.

For each pair, the assembly packs the two operands into opposite 64-bit
lanes. It XORs corresponding byte polynomial products before reconstructing
each of the three Karatsuba components, then reduces their sum once.
Sharing both reconstruction and reduction brings the little-endian loop
to **186 instructions per pair**, versus **256** for two scheduled
iterations. It still performs **48 byte polynomial multiplies per pair**.
There are no spills, per-request allocations or GPR input-load changes.
Six vector-load instructions fetch the 240-byte precomputed tables per
pair. The device results below measure an 18--20% throughput gain over
the scheduled implementation across both driver orders.

The pair loop reads exactly 32 bytes and runs only with two or more blocks
remaining. A single-block invocation branches straight to the unchanged
scheduled implementation. After processing pairs, an odd final block
uses that same path with the updated digest, input pointer and original
H prefix. Partial final bytes still use the frontend's existing padding.
Pairs do not read across the input span supplied to each assembly call.
The 64 KiB SIMD-section limit and digest import/export format are unchanged.

Install release 4 and reload `ghash_neon_shuffle` with the benchmark
unloaded, using `modprobe` so the `gf128mul` dependency is loaded if needed.
Run the boundary/SG checks above. Then compare in both orders:

```sh
for order in ghash-neon-sched,ghash-neon-2x ghash-neon-2x,ghash-neon-sched; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

The 16,384- and 16,400-byte cases compare an even block count and an odd
tail. Benchmark release 2 works unchanged. Select this variant explicitly
as `ghash-neon-2x`; its priority remains below the stock implementations.

## Byte-PMULL reduction experiment (release 5)

`ghash-neon-2x-pmull` keeps the two-block multiplication and key setup,
replacing only its final shift/XOR reduction. Both variants are generated
from the same pair-loop macro. The four previous assembly functions retain
their exact instruction encodings. Single-block calls and odd final blocks
still use the scheduled implementation, so this experiment exercises the
new reducer only when an assembly invocation contains at least two blocks.

The reflected reduction constant is `0xc200000000000000`, or `0xc2 << 56`.
Two byte-form `PMULL`/`PMULL2` instructions multiply all 16 bytes of the low
product half by `0xc2`. `UZP1`/`UZP2` separate the low and high product bytes.
A one-byte rotation and XOR combine adjacent byte products and wrap the
lowest product byte into the high-byte feedback term. A third byte PMULL
calculates the remaining feedback correction; `EXT` places its low two
bytes at bits 112..127. XOR with the low and high product halves completes
the same reflected Montgomery reduction as the shift-based version.

This uses ordinary ASIMD byte polynomial multiplication, not the optional
64x64 PMULL instruction. The constant is initialized once per pair-loop
invocation in a register unused by the pair multipliers. There are no extra
key tables, input loads, assembly spills, allocations or data-dependent
memory accesses. The key context remains 256 bytes.

| Work per pair | `ghash-neon-2x` | `ghash-neon-2x-pmull` |
| --- | ---: | ---: |
| Reduction instructions | 19 | 15 |
| Little-endian loop instructions | 186 | 182 |
| Byte polynomial multiply instructions | 48 | 51 |

The device comparison below shows a repeatable 1.6--1.8% gain over
`ghash-neon-2x`, isolating the change in reduction.

Install release 5, unload the benchmark if it is still loaded, and reload
the comparison module:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
```

Check different keys, block/pair boundaries, odd tails and messages up to
64 KiB before measuring throughput:

```sh
for key_seed in 0 1708171853 4294967295; do
    insmod ghash_bench drivers=ghash-neon-2x-pmull seed="$key_seed" \
        lengths=1,2,3,15,16,17,31,32,33,47,48,49,63,64,65,1440,16384,16400,65536 \
        depths=1,32 duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

Then exercise SG buffering and GCM-formatted input:

```sh
for chunk in 17 47; do
    insmod ghash_bench drivers=ghash-neon-2x-pmull \
        lengths=4097 sg_chunk="$chunk" depths=1,32 \
        duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
for aad in 5 13; do
    insmod ghash_bench drivers=ghash-neon-2x-pmull \
        lengths=31,32,33,1440,16384,16400,65536 aad_len="$aad" depths=1 \
        duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

These checks use the benchmark's independent bit-serial reference and
guards. Compare contiguous throughput in both orders on the same CPU:

```sh
for order in ghash-neon-2x,ghash-neon-2x-pmull ghash-neon-2x-pmull,ghash-neon-2x; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Benchmark release 2 works unchanged. The default `ghash` selection and
production GCM/kTLS path remain unchanged.

## Single-block byte-PMULL reduction experiment (release 6)

`ghash-neon-pmull` compares the same reducer without aggregation. It uses
the scheduled single-block loop and original 16-byte key context: every
block computes `(Y XOR X) * H` and reduces immediately. There is no H-squared
setup, pair key table or branch to a different implementation for odd tails.
Partial final bytes still use the frontend's existing zero padding, then
the new single-block reducer.

Both single-block variants are generated from one loop macro. Input loads,
guarded preload, the three Karatsuba products and key setup match
`ghash-neon-sched`. The five existing functions retain their exact assembly
encodings. The shared reducer accepts its constant register as an argument:
the pair loop keeps `0xc2` in `SHASH2`, while the single-block loop must retain
the cross-product key in `SHASH2` for the next iteration. It therefore
initializes `T2` with `0xc2` after each block's Karatsuba products. This adds
one instruction to the 15-instruction reducer, without extra memory loads
or spills. Preloaded input in `NEXT` and all key/permutation registers survive
the reduction.

| Work per block | `ghash-neon-sched` | `ghash-neon-pmull` |
| --- | ---: | ---: |
| Reduction plus constant setup | 19 | 16 |
| Nonfinal loop instructions | 128 | 125 |
| Byte polynomial multiply instructions | 24 | 27 |
| Key context bytes | 16 | 16 |

Install release 6, unload the benchmark if it is still loaded, and reload
`ghash_neon_shuffle`. Check short inputs, full blocks and different keys:

```sh
for key_seed in 0 1708171853 4294967295; do
    insmod ghash_bench drivers=ghash-neon-pmull seed="$key_seed" \
        lengths=1,2,3,15,16,17,31,32,33,47,48,49,63,64,65,1440,16384,16400,65536 \
        depths=1,32 duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

Repeat the preceding section's SG and GCM-formatted checks with
`drivers=ghash-neon-pmull`. Compare against scheduled in both orders to
measure reduction independently of aggregation:

```sh
for order in ghash-neon-sched,ghash-neon-pmull ghash-neon-pmull,ghash-neon-sched; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Benchmark release 2 works unchanged. This variant remains below stock
priority and requires only ASIMD byte PMULL. Device results below show a
2.1--2.2% gain over scheduled in both orders, including the 16-byte case.

## Original-loop byte-PMULL reduction experiment (release 7)

`ghash-neon-orig-pmull` isolates the reducer on the stock Linux 6.18 p8
implementation. Its block loads, input transformations, three Karatsuba
products, multiplication order, ZIP/EXT reconstruction and key rotations
match stock `ghash-neon`. It does not use the fused TBL shuffles, scheduling,
preloaded input or two-block aggregation from the other experiments.

Only the reduction changes to the shared 15-instruction byte-PMULL reducer.
The original products leave `v28` unused, so its replicated `0xc2` constant
can be initialized once per assembly call, without a per-block setup
instruction. All live key, mask and permutation registers survive reduction.
Every block, including a zero-padded final block, follows the same path.
The standalone four-argument assembly entry point omits stock's optional
head-block dispatch, which is unused for this shash interface; it shares
the other comparison drivers' frontend and 16-byte single-block key context.

| Work per block | Stock `ghash-neon` | `ghash-neon-orig-pmull` |
| --- | ---: | ---: |
| Reduction instructions | 19 | 15 |
| Little-endian loop instructions | 139 | 135 |
| Byte polynomial multiply instructions | 24 | 27 |

Install release 7 and reload `ghash_neon_shuffle` after unloading any active
benchmark. First check fragmented and partial input:

```sh
insmod ghash_bench drivers=ghash-neon-orig-pmull \
    lengths=1,15,16,17,31,32,33,47,48,49,4097 sg_chunk=47 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
```

Repeat the preceding sections' seed, SG and GCM-formatted checks with the
new driver name. Compare directly with the built-in original in both orders:

```sh
for order in ghash-neon,ghash-neon-orig-pmull ghash-neon-orig-pmull,ghash-neon; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Benchmark release 2 works unchanged. All six previous assembly functions
retain their exact instruction encodings. Device results below pass the
short/fragmented checks and show a 2.8--2.9% gain over stock at 1440 through
65536 bytes in both comparison orders.

## Fused multiplication/reduction experiment (release 8)

`ghash-neon-fused` implements the key-precomputation idea in section 2.2.2
of Intel's [Optimized GHASH Function guide](https://builders.intel.com/docs/networkbuilders/advanced-encryption-standard-galois-counter-mode-optimized-ghash-function-technology-guide-1693300747.pdf).
It processes one block at a time, retaining the existing key reflection,
digest representation, frontend buffering and 64 KiB SIMD-section limit.
It does not use two-block aggregation.

Let `t = x^64`, `c = 0xc200000000000000`, and write the reflected field
polynomial as `P = t^2 + c*t + 1`. All products below are carry-less and
addition is XOR. For the existing shifted/reflected key `H = H0 + H1*t`,
key setup precomputes `K = H0*(t + c) + H1`. This absorbs the first
Montgomery fold because `A*H/t = A0*K + A1*H (mod P)`. The loop calculates:

```text
L = A0*K0 XOR A1*H0
U = A0*K1 XOR A1*H1
result = clmul(L.low64, c) XOR swap64(L) XOR U
```

The result is `A*H/t^2 (mod P)`, matching the other variants. The C key
setup uses shifts/XORs and stores the two operand pairs and four rotations
of each pair. Each assembly iteration reads these two 80-byte tables.
Within each sum, the existing two-block product helper XORs corresponding
byte products before their shared reconstruction. Four 64x64 products
therefore need only two reconstructions. The remaining nine-instruction
fold uses one byte PMULL by `0xc2`, followed by byte packing and XORs.
Its constant and zero register are initialized once per call.

| Work per block | `ghash-neon-orig-pmull` | `ghash-neon-fused` |
| --- | ---: | ---: |
| Little-endian loop instructions | 135 | 116 |
| Byte polynomial multiply instructions | 27 | 33 |
| Key context bytes | 16 | 176 |
| Key-table bytes loaded inside each iteration | 0 | 160 |

The smaller instruction count trades against more multiplies and key loads;
it does not establish a speedup. All seven previous assembly functions
retain their exact instruction encodings. The new driver has its own
setkey/update/finup callbacks, priority 92 and explicit module aliases.
Benchmark release 2 works unchanged. The supplied device run passes short/SG
and contiguous-input checks, but the fused variant is 1.8% slower at 16 bytes
and 7.7--8.1% slower at 1440 through 65536 bytes in both benchmark orders.
The measurements are recorded below; this remains a comparison experiment.

Install release 8 and reload `ghash_neon_shuffle` after unloading any active
benchmark. First exercise different keys, short inputs, SG boundaries and
large messages using the independent reference and guard checks:

```sh
for key_seed in 0 1708171853 4294967295; do
    insmod ghash_bench drivers=ghash-neon-fused seed="$key_seed" \
        lengths=1,2,3,15,16,17,31,32,33,47,48,49,63,64,65,1440,16384,16400,65536 \
        depths=1,32 duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

Repeat with `lengths=4097 sg_chunk=17` and `lengths=4097 sg_chunk=47`.
Also run `lengths=31,32,33,1440,16384,16400,65536 aad_len=13` to cover
GCM-formatted input, including a hash operation exceeding 64 KiB. Then
compare with the original-loop reducer in both orders on the same CPU:

```sh
for order in ghash-neon-orig-pmull,ghash-neon-fused ghash-neon-fused,ghash-neon-orig-pmull; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Use the same commands with `ghash-neon-2x-pmull` as the control to compare
with the fastest earlier bulk variant. Key setup and reference checks are
outside the timing, as in the preceding measurements.

## Two-block key-table reuse experiment (release 9)

`ghash-neon-2x-reuse` retains the arithmetic of `ghash-neon-2x-pmull`, including
its 51 byte PMULLs and one reduction per 32 input bytes. It shares the same
256-byte key context and key setup. The three independent Karatsuba product
sums run in alternating order:

```text
pair 1: low -> high -> cross
pair 2: cross -> high -> low
pair 3: low -> high -> cross
```

The unrotated key and its four rotations remain in `v0` and `v20..v23` after
the product, final reduction and next pair's input preparation. Each pair
therefore starts with the preceding pair's last table already available.
The first low-product table is loaded once before the loop. For `n` pairs,
key-table reads total `80 + 160*n` bytes instead of `240*n`: the first pair
still reads 240 bytes and each subsequent pair reads 160. No extra vector
registers, products, key precomputation or source reads are needed.

Two loop bodies encode the alternating order, with explicit key-pointer
adjustments in the reverse body. On little-endian ARM64 they have 179 and
181 instructions, versus 182 for the baseline pair loop. On big-endian they
have 177 and 179 instructions, versus 180. This reduces table-load work but
increases code size; a throughput gain needs device measurements. Single
blocks and odd tails use the same scheduled path as `ghash-neon-2x-pmull`.
All eight earlier assembly functions retain their instruction encodings.

Install release 9 and reload `ghash_neon_shuffle` after unloading the
benchmark. Benchmark release 2 needs no changes; its eight-name limit means
the nine experimental drivers must be compared in subsets. Start with short,
fragmented and call-boundary checks against the independent reference:

```sh
for key_seed in 1708171853 1 4294967295; do
    insmod ghash_bench drivers=ghash-neon-2x-reuse seed="$key_seed" \
        lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,95,96,97,127,128,129,4097 \
        sg_chunk=47 depths=1 duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done

# GCM-formatted messages exceed 64 KiB; ahash splits SG input at pages.
insmod ghash_bench drivers=ghash-neon-2x-reuse \
    lengths=65520,65521,65535,65536 aad_len=13 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1 &&
rmmod ghash_bench
```

Then compare directly with the previous two-block PMULL implementation in
both orders, keeping CPU affinity and workload settings the same:

```sh
for order in ghash-neon-2x-pmull,ghash-neon-2x-reuse ghash-neon-2x-reuse,ghash-neon-2x-pmull; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

The supplied device run passes short/SG and contiguous-input checks. Compared
with `ghash-neon-2x-pmull`, it gains about 1.0% at 64 bytes and 4.8--5.5% at
1440 through 65536 bytes in both benchmark orders. Detailed results follow.

## Two-block key-load scheduling experiment (release 10)

`ghash-neon-2x-load` keeps release 9's alternating product order and table
reuse. It changes how each of the two remaining 80-byte tables reaches
`v0` and `v20..v23`:

- Six scalar 64-bit loads into caller-saved `x6..x11` run between the current
  product's shuffles and multiplies. The current vector keys remain intact.
- After their last multiply, four 64-bit NEON loads and six lane inserts
  assemble the next table in the same vector registers. Each low-half load
  is followed by an independent insert into another register. A load into
  `dN` clears the upper half of `vN`, so its upper-half insert comes later.
- The third product retains its table for the next pair and stages nothing.
  The last pair therefore makes no speculative next-pair reads.

The immutable table still contains native-endian `u64` values, so scalar and
64-bit NEON loads preserve the previous `LD1 .2d` interpretation on both
endiannesses. The key context, setkey, input preparation, arithmetic,
single/odd-tail path and 64 KiB SIMD-section limit are unchanged. There are
no extra vector registers or stack spills. Table traffic remains
`80 + 160*n` bytes for `n` pairs, with 51 byte PMULLs per pair.

This follows the mixed scalar/NEON loading pattern discussed in
[Coding NEON kernels for the Cortex-A53](https://destevez.net/2025/02/coding-neon-kernels-for-the-cortex-a53/).
It targets load overlap on A53's partially dual-issue pipeline. It adds
instructions: the forward/reverse bodies contain 209/211 instructions on
little-endian ARM64 (207/209 on big-endian), versus release 9's 179/181
(177/179). Transfer and issue costs trade against load overlap. The supplied
device comparison measures a 6.2--6.8% gain at 1440 through 65536 bytes in
both orders; it does not identify individual dual-issue pairs or stall costs.
All nine previous implementations retain
their instruction encodings and remain available as controls.

Install release 10, unload `ghash_bench` if loaded, then reload the comparison
module. Benchmark release 2 works unchanged and still accepts at most eight
driver names per insertion. First check short, fragmented and call-boundary
inputs against its independent reference:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle

for key_seed in 1708171853 1 4294967295; do
    insmod ghash_bench drivers=ghash-neon-2x-load seed="$key_seed" \
        lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,95,96,97,127,128,129,4097 \
        sg_chunk=47 depths=1 duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done

# GCM-formatted messages exceed 64 KiB; ahash splits SG input at pages.
insmod ghash_bench drivers=ghash-neon-2x-load \
    lengths=65520,65521,65535,65536 aad_len=13 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1 &&
rmmod ghash_bench
```

After those checks pass, measure against `ghash-neon-2x-reuse` in both orders:

```sh
for order in ghash-neon-2x-reuse,ghash-neon-2x-load ghash-neon-2x-load,ghash-neon-2x-reuse; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Device reference/guard checks pass for three seeds with fragmented input,
GCM-formatted input crossing the 64 KiB boundary, and both benchmark orders.
Detailed results follow.

## Four-block aggregation experiment (release 11)

`ghash-neon-4x` evaluates four GHASH steps together:

```text
Y' = (Y xor X1)*H^4 xor X2*H^3 xor X3*H^2 xor X4*H
```

Setkey computes the powers in GHASH's field representation, then reflects
each power into the assembly representation. The 736-byte context starts
with the complete 256-byte two-block context. The remaining 480 bytes hold
six tables: low/high/cross products, each with an H^4/H^3 pair and an H^2/H
pair. Every table contains the two unrotated 64-bit operands and their four
byte rotations. Duplicating the H^2/H tables allows sequential table loads
in the main loop and leaves the two-block tail context unchanged.

For each Karatsuba part, the loop accumulates all four terms' byte products
before reconstructing their 128-bit sum. The second pair uses a separate
scratch schedule that preserves the first pair's unreconstructed terms.
It uses all 32 vector registers without spills. The three completed sums
feed the existing Karatsuba join and one byte-PMULL reduction. Input loads
read exactly four blocks; short calls and the final one to three blocks
branch to `ghash-neon-2x-load` using the context prefix.

The loop retains the preceding experiment's GPR/D-load scheduling and
alternates the order of all six tables, including the order of the two
term pairs. The last table stays in registers for the next iteration.
For each four blocks, compared with two iterations of `ghash-neon-2x-load`:

| Work | Two pairs | Four-block group |
| --- | ---: | ---: |
| Byte PMULL instructions | 102 | 99 |
| Product reconstructions | 6 | 3 |
| Field reductions | 2 | 1 |
| Little-endian loop instructions, average | 420 | 373.5 |
| Table bytes after the initial cached table | 320 | 400 |

The forward/reverse bodies contain 371/376 instructions on little-endian
and 367/372 on big-endian. Table traffic is `80 + 400*n` bytes for `n`
four-block groups, excluding a tail's separate setup. The larger context,
loop body and extra key loads trade against the arithmetic savings. Device
comparisons pass and show 19.3% higher throughput at 1440 bytes and about
22.5% at 16384 through 65536 bytes in both orders. Detailed results follow.

With the new module loaded, check boundaries and fragmented input first:

```sh
for seed in 1708171853 1 4294967295; do
  insmod ghash_bench drivers=ghash-neon-2x-load,ghash-neon-4x \
    lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,79,80,81,95,96,97,111,112,113,127,128,129,65535,65536 \
    depths=1,32 seed="$seed" duration_ms=10 warmup_ms=0 runs=1 || break
  rmmod ghash_bench || break
done
for chunk in 47 79 113 1440; do
  insmod ghash_bench drivers=ghash-neon-2x-load,ghash-neon-4x \
    lengths=4097 sg_chunk="$chunk" depths=1 \
    duration_ms=10 warmup_ms=0 runs=1 || break
  rmmod ghash_bench || break
done
```

Then compare both orders on the same CPU, with key setup outside timing:

```sh
for order in ghash-neon-2x-load,ghash-neon-4x ghash-neon-4x,ghash-neon-2x-load; do
  taskset -c 0 insmod ghash_bench drivers="$order" \
    lengths=16,32,64,96,128,1440,16384,16400,65536 depths=1 \
    duration_ms=1000 warmup_ms=100 runs=5 || break
  rmmod ghash_bench || break
done
```

The 16/32-byte rows exercise the unchanged tail implementation; 64 bytes
is the first complete four-block group, and 96 bytes adds a two-block tail.
Keep benchmark release 2; its eight-driver selection limit is unchanged.

## Device evidence and build checks

The release-1 device comparison passed reference/final-slot and guard checks
across 40 timed rows and 1,076,324 timed requests. Intermediate digests were
reused, so this does not verify every timed request. With raw, contiguous
input, depth 1, five one-second trials and a 100 ms warmup, median payload
throughputs were:

| Input bytes | Stock Gbit/s | Shuffle Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 1440 | 1.026416 | 1.026256 | -0.016% |
| 16384 | 1.080545 | 1.080813 | +0.025% |
| 16400 | 1.080661 | 1.080511 | -0.014% |
| 65536 | 1.082011 | 1.082456 | +0.041% |

The shuffle rewrite shows no measurable gain in this capture. It does not
test `ghash-neon-sched`, partial final blocks or fragmented input.

Two subsequent release-2 device captures, with forward and reversed driver
order, pass 120 timed rows and 3,237,942 timed requests using the same lengths
and timing parameters. The scheduled variant's medians are consistently
above stock in both orders:

| Input bytes | Stock Gbit/s, forward | Scheduled Gbit/s, forward | Stock Gbit/s, reversed | Scheduled Gbit/s, reversed |
| ---: | ---: | ---: | ---: | ---: |
| 1440 | 1.027274 | 1.034070 | 1.026963 | 1.033628 |
| 16384 | 1.081070 | 1.088575 | 1.081021 | 1.088676 |
| 16400 | 1.081127 | 1.088655 | 1.080995 | 1.088621 |
| 65536 | 1.082601 | 1.090910 | 1.082651 | 1.090621 |

The gain is 0.65--0.77% across these measurements. At every length, the
slowest scheduled trial exceeds the fastest stock or shuffle trial in
the same capture; shuffle alone stays essentially tied with stock. These
captures pass the benchmark's reference/final-slot and guard checks, not
independent verification of every intermediate timed digest. They do not
exercise `ghash-neon-gpr`, partial final blocks or fragmented input.

The release-3 GPR comparison passes 80 timed rows and 2,168,198 requests
across both orders with the same four contiguous lengths. Median GPR
changes relative to scheduled are +0.012%, +0.028%, +0.017%, +0.009% when
scheduled runs first, and -0.026%, -0.007%, +0.016%, -0.011% when GPR runs
first. Trial ranges overlap at every length. This provides no convincing
throughput improvement from GPR loading. It covers the benchmark's
reference/final-slot and guard checks, without independent verification of
each intermediate timed digest; no short-block/SG capture was supplied.

The release-4 two-block comparison passes 91 rows and 2,462,468 timed
requests. Eleven short/fragmented-input rows use lengths
1/15/16/17/31/32/33/47/48/49/4097 with `sg_chunk=47`, including 88 SG entries
at 4097 bytes. The other 80 rows compare scheduled and two-block paths in
both orders, using contiguous input, depth 1, five one-second trials and
100 ms warmups. Medians pooled across both orders are:

| Input bytes | Scheduled Gbit/s | Two-block Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 1440 | 1.034099 | 1.223808 | +18.35% |
| 16384 | 1.088930 | 1.301576 | +19.53% |
| 16400 | 1.088646 | 1.300811 | +19.49% |
| 65536 | 1.090963 | 1.301028 | +19.25% |

Every two-block trial exceeds every scheduled trial at the same length,
with a consistent gain in both orders. The checks cover preflight,
final-slot digests and guards; intermediate timed digests are reused.
These results do not include the release-5 byte-PMULL reducer.

The release-5 pair-reducer comparison passes another 91 rows and 2,678,931
timed requests. The same eleven short/47-byte-SG cases pass, including
4097 bytes across 88 SG entries. Eighty contiguous-input performance rows
compare the two pair variants in both orders with depth 1, five one-second
trials and 100 ms warmups. Pooled medians are:

| Input bytes | Two-block shift/XOR Gbit/s | Two-block PMULL Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 1440 | 1.223453 | 1.243362 | +1.63% |
| 16384 | 1.301420 | 1.323555 | +1.70% |
| 16400 | 1.300654 | 1.322687 | +1.69% |
| 65536 | 1.301044 | 1.323669 | +1.74% |

Every PMULL trial exceeds every shift/XOR trial at the same length, and
the gain holds in both orders. Preflight/final-slot digests and guards pass;
intermediate timed digests are reused. This supports the pair reducer on
the measured A53. The subsequent single-block comparison is recorded below.

The release-6 scheduled single-block PMULL comparison passes 111 rows and
29,568,903 timed requests. Eleven short/47-byte-SG rows cover the same
1/15/16/17/31/32/33/47/48/49/4097-byte grid. One hundred contiguous-input
performance rows compare scheduled shift/XOR and PMULL reduction in both
orders, adding 16-byte input to the four longer lengths. They use depth 1,
five one-second trials and 100 ms warmups. Medians pooled across both orders:

| Input bytes | Scheduled shift/XOR Gbit/s | Single-block PMULL Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.172820 | 0.176500 | +2.13% |
| 1440 | 1.033300 | 1.055473 | +2.15% |
| 16384 | 1.088875 | 1.112490 | +2.17% |
| 16400 | 1.088838 | 1.112632 | +2.19% |
| 65536 | 1.091125 | 1.114901 | +2.18% |

Every PMULL trial exceeds every scheduled shift/XOR trial at the same
length, and the gain holds in both orders. Preflight/final-slot and guard
checks pass; intermediate timed digests are reused. All requests are
synchronous with no backlog notifications. The subsequent original-loop
comparison is recorded below.

The release-7 original-loop PMULL comparison passes another 111 rows and
30,186,087 timed requests. Eleven short/47-byte-SG cases cover
1/15/16/17/31/32/33/47/48/49/4097-byte inputs, including 88 SG entries for
4097 bytes. One hundred contiguous-input performance rows compare stock
`ghash-neon` with `ghash-neon-orig-pmull` in both orders using depth 1,
five one-second trials and 100 ms warmups. Medians pooled across both orders:

| Input bytes | Stock Gbit/s | Original-loop PMULL Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.177602 | 0.179761 | +1.22% |
| 1440 | 1.027320 | 1.056141 | +2.81% |
| 16384 | 1.081125 | 1.112736 | +2.92% |
| 16400 | 1.081037 | 1.112443 | +2.91% |
| 65536 | 1.083000 | 1.114393 | +2.90% |

Every original-loop PMULL trial exceeds every stock trial at the same
length. The gain holds in both orders: 1.15--1.27% at 16 bytes and
2.80--2.94% for the larger inputs. Preflight/final-slot digests and guards
pass, with intermediate timed digests reused. All operations are synchronous
with no backlog notifications. This covers the supplied raw-input lengths,
fragmentation and seed; it is not an exhaustive Crypto API validation.

At 1440 through 65536 bytes, these medians differ by less than 0.07% from
the preceding scheduled single-block PMULL capture. This is a comparison
across captures, not a direct head-to-head trial, but suggests the stock
loop with the replacement reducer delivers essentially the same bulk
throughput without the shuffle/scheduling changes. The two-block PMULL
variant remains faster in the earlier bulk measurements.

The release-8 fused comparison passes 111 rows and 30,006,298 timed requests
with seed 1708171853. Eleven short/47-byte-SG cases cover
1/15/16/17/31/32/33/47/48/49/4097-byte raw inputs, including 88 SG entries for
4097 bytes. One hundred contiguous-input rows compare `ghash-neon-orig-pmull`
with `ghash-neon-fused` in both orders at depth 1, using five one-second trials
and 100 ms warmups. Medians pooled across both orders (ten trials per cell):

| Input bytes | Original-loop PMULL Gbit/s | Fused Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.179472 | 0.176200 | -1.82% |
| 1440 | 1.055980 | 0.974978 | -7.67% |
| 16384 | 1.112553 | 1.023369 | -8.02% |
| 16400 | 1.112610 | 1.023426 | -8.02% |
| 65536 | 1.114199 | 1.023905 | -8.10% |

Every fused trial is slower than every original-loop PMULL trial at the
same length. The slowdown persists in both orders: 1.81--1.83% at 16 bytes
and 7.65--8.12% for the larger inputs. Preflight/final-slot digests and guards
pass; intermediate timed digests are reused. All operations are synchronous
with no backlog notifications. These checks cover the supplied raw-input
lengths, fragmentation and seed, rather than exhaustive Crypto API validation.

The shorter fused loop does not improve throughput on this Cortex-A53.
Its additional byte multiplies and per-block key-table loads are plausible
costs, but these timings do not identify the limiting instruction or resource.
They do not establish DRAM bandwidth as the bottleneck. Retain the fused
driver for explicit comparisons; the results do not support replacing the
original-loop reducer. The earlier two-block PMULL measurements remain faster,
although they were collected in a separate capture.

The release-9 table-reuse comparison passes 154 rows and 74,945,036 timed
requests with seed 1708171853. Fourteen short/47-byte-SG cases cover
1/15/16/17/31/32/33/63/64/65/95/96/97/4097-byte raw inputs, including 88 SG
entries for 4097 bytes. Another 140 contiguous-input rows compare
`ghash-neon-2x-pmull` with `ghash-neon-2x-reuse` in both orders at depth 1,
using five one-second trials and 100 ms warmups. Medians pooled across both
orders (ten trials per cell):

| Input bytes | Two-block PMULL Gbit/s | Table reuse Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.173657 | 0.173786 | +0.07% |
| 32 | 0.318226 | 0.318611 | +0.12% |
| 64 | 0.513604 | 0.518611 | +0.97% |
| 1440 | 1.242629 | 1.302167 | +4.79% |
| 16384 | 1.323581 | 1.392698 | +5.22% |
| 16400 | 1.322573 | 1.391228 | +5.19% |
| 65536 | 1.322990 | 1.395769 | +5.50% |

Every reuse trial exceeds every baseline trial at the same length from
32 bytes upward. The gain persists in both orders: 0.97--1.00% at 64 bytes
and 4.78--5.50% for the larger inputs. Sixteen-byte requests use the same
scheduled single-block path, and 32-byte requests still load all three tables
for their only pair; their small differences do not demonstrate table-reuse
savings. The 64-byte case is the first to reuse a table between pairs.

Preflight/final-slot digests and guards pass; intermediate timed digests are
reused, so not every timed request is independently verified. All operations
are synchronous with no backlog notifications. This capture covers the
supplied raw-input lengths, fragmentation and seed, not the additional-seed
or GCM-formatted checks listed above. The results support table reuse as a
useful optimization on this Cortex-A53 and made it the fastest bulk variant
measured through release 9. Timings include the changed product order and loop
layout; they do not isolate the precise load or execution stall costs.

The release-10 key-load scheduling capture passes 204 rows and 75,876,966
timed requests. It includes 60 short/47-byte-SG rows across seeds 1708171853,
1 and 4294967295, four GCM-formatted rows with 13-byte AAD and payloads
65520/65521/65535/65536 bytes, and 140 performance rows in both driver orders.
The latter use contiguous raw input, depth 1, five one-second trials and
100 ms warmups. Ten-trial pooled medians are:

| Input bytes | Table reuse Gbit/s | Key-load scheduling Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.173539 | 0.173813 | +0.16% |
| 32 | 0.318314 | 0.323319 | +1.57% |
| 64 | 0.518019 | 0.531082 | +2.52% |
| 1440 | 1.301973 | 1.384381 | +6.33% |
| 16384 | 1.392595 | 1.487133 | +6.79% |
| 16400 | 1.391333 | 1.485660 | +6.78% |
| 65536 | 1.395888 | 1.488306 | +6.62% |

Every key-load trial exceeds every reuse trial at the same length in this
capture. The gain holds in both orders: 1.32--1.59% at 32 bytes, 2.29--2.57%
at 64 bytes and 6.23--6.80% at 1440 through 65536 bytes. Sixteen-byte requests
use the same scheduled fallback; their small difference does not measure
key-load overlap. This was the fastest bulk variant measured through release
10, reaching about 1.49 Gbit/s, with identical arithmetic and table traffic
to the reuse control. The timings support this schedule on the tested
Cortex-A53 but do not establish which instruction pairs dual-issue or isolate
the stalls.

Preflight/final-slot digests, source contents and guards pass. Intermediate
timed digests are reused, so the request total is not a count of independently
verified digests. All operations are synchronous with no backlogs. These
component measurements do not establish a kTLS or complete GCM speedup.

The release-11 four-block capture passes 180 rows and 111,185,351 timed
requests. Both driver orders use seed 1708171853, 128-byte alignment, raw
single-entry input, depth 1, five one-second trials and a 100 ms warmup.
The following medians pool ten trials per driver and length across the
two orders, calculated from the exact byte counts and elapsed times:

| Input bytes | 2x-load Gbit/s | 4x Gbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 0.174247 | 0.173976 | -0.16% |
| 32 | 0.324040 | 0.323869 | -0.05% |
| 64 | 0.532025 | 0.571067 | +7.34% |
| 96 | 0.678524 | 0.692953 | +2.13% |
| 128 | 0.784071 | 0.859418 | +9.61% |
| 1440 | 1.385182 | 1.651888 | +19.25% |
| 16384 | 1.487719 | 1.823617 | +22.58% |
| 16400 | 1.486524 | 1.821015 | +22.50% |
| 65536 | 1.488876 | 1.823721 | +22.49% |

At every tested length from 64 bytes upward, the slowest four-block trial
exceeds the fastest two-block trial across both orders. Within each order,
gains are 7.28--7.40% at 64 bytes, 2.11--2.15% at 96 bytes, 9.31--9.39% at
128 bytes and 19.25--22.58% at 1440 through 65536 bytes. The pooled median
ratio can differ from the per-order ratios, particularly at 128 bytes.
Sixteen- and 32-byte requests use the existing fallback, preceded by the
four-block entry check; their small differences show no aggregation gain.
At 96 bytes, the loop processes four blocks then re-enters the two-block
implementation, including its setup, which is consistent with the smaller
gain at that length. The timings do not isolate each setup or instruction cost.

Four-block aggregation is now the fastest measured bulk variant at about
1.824 Gbit/s. Preflight/final-slot digests, source contents and guards pass;
intermediate timed digests are reused. All operations are synchronous with
zero backlogs. This device capture covers the listed raw lengths and one
seed, without the additional-seed, byte-tail, fragmented-SG or GCM-formatted
matrix; the broader QEMU checks are separate. It does not measure complete
GCM or kTLS throughput. No code change or package-release bump was needed
to record these measurements.

Release 1 passes an ARM64 GCC 14 build against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`, including assembly, modpost and linking. Static
inspection checks the private assembly symbol, module aliases/dependencies,
and the unchanged 127-instruction optimized block loop. Benchmark release 2
also builds on ARM64 and x86-64 with those flags. No modules were loaded,
and no target correctness tests or benchmarks were executed locally.
Release 2 also passes that ARM64 build and strict checkpatch. Static
disassembly confirms the unchanged shuffle loop, 24 byte polynomial
multiplies in the scheduled loop, direct calls, and the guarded input load.
Symbolic checks of the disassembled multiply groups and reduction preserve
their results for arbitrary operands. These are build and static checks,
not an OpenWrt package/image build or execution of target code.

Release 3 passes the same ARM64 GCC 14 module build with warnings treated
as errors, modpost and linking, plus strict checkpatch. Its big-endian
assembly also builds separately. Static checks confirm both previous
assembly functions are byte-for-byte unchanged, all wrappers use direct
calls, and the new products/reductions match the scheduled variant.
Symbolic byte permutations preserve input ordering on both endiannesses;
the disassembled input-address/count paths read exactly the supplied
blocks in cases spanning 1 through 4096 blocks, including the final-block
path. The new assembly has no stack accesses. These checks execute no
target code. The subsequent contiguous-input device comparison is recorded
above.

Release 4 passes the same ARM64 GCC 14 warning-as-error module build,
modpost/link and strict checkpatch, plus a separate big-endian assembly
build. Disassembly confirms all three existing assembly functions retain
their instruction encodings. Symbolic bit-polynomial checks prove each of
the three fused products equals the XOR of the two independent 64x64
products for arbitrary operands. Input packing, the unchanged reduction,
direct wrapper calls and bounded pair/odd-tail control flow are checked
on both endiannesses, including 1/2/3 and 4095/4096-block invocations.
`gf128mul_lle()` is called only during 2x key setup. These are build and
static checks; no target code, modules or benchmarks were run locally,
and no OpenWrt package/image build was performed. Subsequent device results
for two-block aggregation are recorded above.

Release 5 passes the ARM64 GCC 14 build against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`, including modpost and linking, plus strict checkpatch
and a separate big-endian assembly build. The disassembled 15-instruction
reducer is symbolically equivalent to the old reducer for arbitrary inputs.
All 256 product basis bits also satisfy the independent polynomial identity
`result * x^128 = product (mod x^128 + x^127 + x^126 + x^121 + 1)`.
Byte PMULL by a constant is linear, so this proves the identity for every
256-bit product. Checks cover both endiannesses, unchanged pair products
and input packing, bounded input/key accesses, single-block and odd-tail
branches, direct wrappers, the shared key setup and all five aliases.
The previous four assembly functions are byte-for-byte unchanged. These
are build and static checks only; no target code, modules or benchmarks
were run locally and no OpenWrt package/image build was performed.
Subsequent device checks and measurements of the pair reducer are recorded
above.

Release 6 passes the ARM64 GCC 14 module build against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`, including modpost and linking, plus strict checkpatch
and a separate big-endian assembly build. Disassembly confirms the five
previous functions remain byte-for-byte unchanged. The new loop's setup,
input loading, key rotations and all three products match scheduled.
Symbolic checks prove the reducer equivalent for arbitrary inputs and
verify preservation of live keys, permutation constants and preloaded input.
All 256 product basis bits satisfy the independent reflected-polynomial
identity on both endiannesses. Control-flow checks cover 1/2/3/4/5/31/32/33
and 4095/4096-block calls, with one reduction per block and no overreads,
assembly spills or external tail branches. All six wrappers use direct calls;
the new driver uses the original single-block key setup. No target code,
modules or benchmarks were run locally, and no OpenWrt package/image was built.

Release 7 passes the same ARM64 GCC 14 module build with warnings treated
as errors, modpost/link and strict checkpatch, plus a separate big-endian
assembly build. All six previous assembly functions retain their instruction
encodings. Comparing against freshly assembled stock Linux code confirms
identical key preparation and loop instructions up to reduction on both
endiannesses; the new setup adds one constant initialization and omits the
unused optional head-block dispatch. Symbolic checks prove the reducer
equivalent to stock for arbitrary inputs, including stock's lane inserts,
and preserve live keys, masks, permutations and the reduction constant.
All 256 product basis bits also satisfy the independent reflected-polynomial
identity. Control-flow checks cover 1/2/3/4/5/31/32/33 and 4095/4096-block
calls, with bounded input reads, one reduction per block and no assembly
spills or tail fallback. All seven wrappers use direct calls and unique
driver aliases; dependencies remain `crypto_hash` and `gf128mul`.
These are build and static checks only, without target execution or an
OpenWrt package/image build.

Release 8 passes the ARM64 GCC 14 module build with `W=1 KCFLAGS=-Werror`,
including modpost and linking, and a separate big-endian assembly build.
Strict checkpatch passes. Static disassembly confirms all seven earlier
functions are unchanged, all eight wrappers call directly, only ASIMD
byte PMULL is used, and there are no assembly stack accesses. Symbolic
analysis proves both fused product sums for arbitrary operands and the
one-PMULL final fold, including preservation of live input and constants.
The C precomputation expressions and the full multiply/reduce identity
hold for all 128-by-128 operand basis pairs; the operation is bilinear,
so this establishes the algebra for arbitrary operands. Input byte order
and bounded input/key accesses are checked for both endiannesses and
1/2/3/4/5/31/32/33/4095/4096-block calls. The loop uses 116 instructions
on little-endian and 115 on big-endian, with 33 byte PMULLs per block.
Module dependencies remain `crypto_hash` and `gf128mul`. No target code,
modules or benchmarks were executed locally, and no OpenWrt package/image
build was performed. Subsequent device checks and measurements of the fused
variant are recorded above.

Release 9 passes the ARM64 GCC 14 module build against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`, including modpost and linking, plus strict checkpatch
and a separate big-endian assembly build. Static disassembly confirms all
eight earlier functions retain their encodings on both endiannesses. Both
new loop bodies preserve the baseline product instructions and final
Karatsuba join/reducer. Symbolic checks prove each product sum for arbitrary
operands and preserve the other inputs, completed products, keys and constants.
Input packing is checked on both endiannesses. Compiled control-flow analysis
checks 76 block counts from one through 4096, including every count one through
65, odd tails and alternating phases. It verifies the key identity used at
every product, bounded source/key reads, one fold per pair and the exact
`80 + 160*n` table-byte count. There are no assembly stack accesses; all nine
update/finup wrappers call directly. The new driver uses the existing two-block
key setup/context, with priority 91 and distinct aliases. No target code,
modules or benchmarks were executed locally, and no OpenWrt package/image
build was performed. Subsequent device checks and measurements of table
reuse are recorded above.

Release 10 passes the ARM64 GCC 14 module build against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`, including modpost and linking, strict checkpatch and a
separate big-endian assembly build. Static disassembly confirms all nine
previous functions retain their encodings on both endiannesses. Symbolic
checks prove the three product sums for arbitrary operands, preserve live
inputs/results/constants and match the existing join/reducer. Compiled
control-flow analysis checks 76 block counts through 4096 on both endiannesses,
including all counts one through 65. It follows every key lane at each
multiply, including the upper-half clearing of `LDR D`, and verifies that
key replacement starts after the old key's last use. Source/table reads stay
within bounds; each next table is read exactly once, the last product stages
nothing, and table traffic remains `80 + 160*n` bytes. All ten update/finup
wrappers call directly; the new driver shares the two-block key setup/context
and uses explicit priority 90. These initial checks were build/static only;
subsequent QEMU execution is recorded below. No OpenWrt package/image build
was performed.

## QEMU TCG correctness validation (release 10)

QEMU 10.0.13 TCG boots Linux 6.18.63 on `virt` with `cortex-a53`, two CPUs
and 1 GiB RAM. The guest enables generic KASAN, `CRYPTO_SELFTESTS` and
`CRYPTO_SELFTESTS_FULL`, with `cryptomgr.notests=0`, `noslowtests=0` and
`fuzz_iterations=100`. Both the comparison module and benchmark release 2
are rebuilt against that guest kernel with `W=1 KCFLAGS=-Werror`.

The existing benchmark passes 36 insertions, 720 rows and 41,898 timed
requests. All ten comparison drivers are selected explicitly alongside
`ghash-generic`; all ten report `selftest: passed`. Coverage includes:

- Every length 1..65 and block/pair/page boundaries through 65536 bytes
  with three reproducible keys/payload seeds.
- SG chunks 1/7/15/16/17/31/32/33/47/64/127 bytes, plus 1440/1460/4096-byte
  chunks for large messages and control comparisons.
- Request depths 1/32/128, warmup, repeated trials and module unload/reload.
- GCM-formatted messages with 0/5/13/4096-byte AAD, including payloads
  near 64 KiB. The largest formatted input is 69,648 bytes.

Independent reference/final-slot digests, input contents and guards pass.
CSV matrices, requested/resolved drivers, byte counts, rates and request
totals are checked independently. The guest shuts down cleanly without
KASAN reports, kernel warnings or crypto self-test failures. Timed-request
totals exclude the Crypto API's registration self-tests; intermediate
benchmark digests are reused.

The SG ahash frontend walks page-bounded chunks even when the message is
larger than 64 KiB. Such a run tests a long message, not necessarily one
shash call reaching the driver's 64 KiB SIMD-section limit. Direct shash
calls check that boundary in a separate QEMU-only fixture: 2,880 comparisons
of `ghash-neon-2x-load` and `ghash-neon-2x-reuse` against `ghash-generic`
pass. The fixture covers empty input, block/pair boundaries, 65519 through
65553-byte boundary cases and 131071/131072/131073/131089-byte inputs. It
uses three keys (including zero), source offsets 0/1/7/15, whole-buffer
digest and update/final calls, and export/import followed by finup at splits
1/17/65535. Source contents and output guards remain intact. Both drivers
pass 1,440 cases, including updates spanning two 64 KiB chunks plus an odd
tail. This run also completes without KASAN reports or kernel warnings.

No comparison-driver source fixes were needed. QEMU timings do not model
Cortex-A53 scheduling or throughput; device measurements above establish
performance. This guest run is little-endian; big-endian coverage remains
the assembly build and static analysis.

## Eight-block aggregation experiment (release 12)

`ghash-neon-8x` evaluates

```text
(Y XOR X1) * H^8 XOR X2 * H^7 XOR ... XOR X8 * H
```

with one field reduction for all eight input blocks. For each low, high
and cross Karatsuba part, it accumulates eight 64-by-64-bit products in
the unreconstructed byte-product registers, then reconstructs their sum
once. The three parts share the existing join and byte-PMULL reducer.
This uses 195 byte PMULLs and three reconstructions per eight blocks,
compared with 198 byte PMULLs and six reconstructions for two four-block
groups.

The four-block loop already uses all 32 vector registers. The eight-block
variant keeps the incoming accumulator in `SHASH2`, loads two input blocks
at a time with `LD2`, and makes three passes over each 128-byte input span.
It selects the low, high or cross operands during that pass and folds the
incoming accumulator into the first block only. No assembly stack spills
are needed. All three passes stay within the same supplied eight blocks;
there is no speculative load beyond the input.

Key setup retains the complete four-block context as a prefix and adds
powers through H^8. Twelve 80-byte tables contain the low/high/cross parts
of H^8/H^7, H^6/H^5, H^4/H^3 and H^2/H. Traversal alternates forwards and
backwards, retaining the last table and overlapping subsequent key loads
with multiplication. Key context size is 1696 bytes versus 736 for 4x.
Calls and tails below eight blocks branch to the unchanged four-block
implementation, including its two- and single-block tails.

The little-endian forward/reverse bodies contain 712/723 instructions;
the two four-block bodies together contain 747. The eight-block path reads
384 input bytes per 128-byte group and `80 + 880*n` key-table bytes for
`n` groups. Two four-block groups read 128 input bytes and consume 800
key-table bytes in steady state. The device results below show lower
throughput for 128--1440-byte inputs and effectively tied bulk throughput.
The driver remains an explicit, low-priority comparison; 4x remains the
preferred implementation.

After installing release 12, unload any benchmark instance, reload
`ghash_neon_shuffle`, and run the short/boundary check:

```sh
insmod ghash_bench drivers=ghash-neon-4x,ghash-neon-8x \
    lengths=1,15,16,17,63,64,65,111,112,113,127,128,129,143,144,145,239,240,241,255,256,257,4097,65535,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
```

Then compare both orders on the same CPU:

```sh
for order in ghash-neon-4x,ghash-neon-8x ghash-neon-8x,ghash-neon-4x; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,64,112,128,144,192,256,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

128 bytes is the first complete eight-block group, 144 adds a single-block
tail, 192 adds a four-block tail, and 256 exercises both traversal phases.
For fragmented checks, repeat with `lengths=4097 sg_chunk=17` and
`sg_chunk=127`, and with `lengths=16384,16400,65536 sg_chunk=1440`.
GCM-formatted checks can use `aad_len=13 lengths=1,111,112,113,127,128,129,16384,65536`.
Use multiple seeds as for the earlier variants. Source/digest guards and
reference digests must pass before interpreting rates.

### Eight-block device comparison

The supplied Cortex-A53 capture passes 220 rows and 123,849,377 timed
requests across both driver orders. Each order uses seed 1708171853,
raw single-entry input, depth 1, five one-second trials and a 100 ms
warmup. Requested and selected driver names match throughout. Medians
recomputed from byte counts and elapsed time are:

| Input bytes | 4x Gbit/s, 4x first | 8x Gbit/s, 4x first | 8x change | 4x Gbit/s, 8x first | 8x Gbit/s, 8x first | 8x change |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.174570 | 0.173984 | -0.336% | 0.173012 | 0.172495 | -0.299% |
| 64 | 0.566883 | 0.567270 | +0.068% | 0.561666 | 0.563939 | +0.405% |
| 112 | 0.707754 | 0.707409 | -0.049% | 0.702678 | 0.704247 | +0.223% |
| 128 | 0.866641 | 0.823028 | -5.032% | 0.859737 | 0.820087 | -4.612% |
| 144 | 0.861528 | 0.820380 | -4.776% | 0.855647 | 0.817228 | -4.490% |
| 192 | 1.052968 | 0.973264 | -7.569% | 1.046950 | 0.970891 | -7.265% |
| 256 | 1.178032 | 1.134007 | -3.737% | 1.172574 | 1.131227 | -3.526% |
| 1440 | 1.653960 | 1.639256 | -0.889% | 1.652120 | 1.638360 | -0.833% |
| 16384 | 1.823361 | 1.823143 | -0.012% | 1.823204 | 1.822648 | -0.030% |
| 16400 | 1.820322 | 1.819202 | -0.062% | 1.820433 | 1.820200 | -0.013% |
| 65536 | 1.823936 | 1.824981 | +0.057% | 1.822772 | 1.823535 | +0.042% |

At each tested length from 128 through 1440 bytes, every four-block trial
is faster than every eight-block trial in the same order. The 16--64 KiB
medians differ by less than 0.07%, and the reversed-order trial ranges
overlap at all three bulk lengths. This establishes no useful bulk gain
from the extra aggregation. The sub-128-byte cases take the existing
four-block/tail path and do not measure the new eight-block body.

The extra input passes, ten percent more steady-state key-table bytes,
and separate four-block setup for tails are plausible costs despite
fewer arithmetic instructions. The largest regression is at 192 bytes,
which processes one eight-block group and then enters the four-block
tail path. These timings do not isolate load, issue or tail-setup costs;
they are not evidence of a DRAM bandwidth limit. Retain 4x as the preferred
implementation and keep 8x as an explicitly selected experiment.

The exact matrix, byte/time/rate accounting and request totals pass
independent analysis. Verification covers preflight, final-slot digests
and source/digest guards; intermediate timed digests are reused. This
capture uses one seed and block-multiple lengths with contiguous input.
It does not add device partial-block/SG/GCM checks or establish full
GCM/kTLS throughput. No implementation or package-release change was
needed to record these results, and no build/runtime test was rerun.

## Four-block validation (release 11)

The ARM64 GCC 14 module build against Linux 6.18.63 passes with
`W=1 KCFLAGS=-Werror`, including modpost/link. Strict checkpatch and a
separate big-endian assembly build pass. All ten earlier assembly functions
retain their instruction encodings on both endiannesses. Symbolic checks
prove each four-term product sum for arbitrary operands, preserve live
inputs/results/constants and verify input byte order. The Karatsuba join
and reducer match the previously tested pair implementation. Compiled
control-flow analysis checks 139 block counts through 4096 on each
endianness, including every count 1..129. It follows every key lane at
every multiply, checks bounded source/table reads, one reduction per group,
correct one-to-three-block fallback and exactly `80 + 400*n` table bytes.
There are no assembly stack accesses, and all eleven wrappers call directly.

Using the same QEMU TCG/KASAN/full-self-test guest configuration as release
10, release 11 passes 42 benchmark insertions, 922 rows and 53,719 timed
requests. All eleven comparison drivers report `selftest: passed`. The
four-block driver passes every byte length 1..129 with three seeds,
block/page/64 KiB boundaries, SG chunks 1..127 and 1440/1460/4096 bytes,
request depths 1/32/128, repeated trials, GCM formatting and unload/reload.
The maximum formatted input is 69,648 bytes. Exact matrices, selected
drivers, byte counts, rates and request totals are independently checked.

Direct shash calls add 5,400 comparisons against `ghash-generic`: 2,700
each for `ghash-neon-4x` and `ghash-neon-2x-load`. These cover 45 lengths
from empty input through 131,121 bytes, four source offsets, three keys,
whole-buffer digest/update/final and export/import/finup at three splits.
They explicitly cover every full-block tail modulo four and direct calls
spanning the internal 64 KiB boundary. Reference digests, source contents
and guards pass. The guest shuts down without KASAN reports, warnings or
crypto self-test failures. No driver fixes were needed during validation.
QEMU rates are not A53 performance measurements. Big-endian validation
remains build/static only; no OpenWrt package/image was built.

## Eight-block validation (release 12)

The ARM64 GCC 14 module build against Linux 6.18.63 passes with
`W=1 KCFLAGS=-Werror`, including modpost/link. Strict checkpatch and a
separate big-endian assembly build pass. All eleven earlier assembly
functions retain their instruction encodings on both endiannesses.
Symbolic checks prove the eight-term product sums for arbitrary operands,
including input packing, the incoming accumulator, and preserved live
registers. The join and reducer match the previously tested pair path.
Compiled control-flow analysis checks 271 block counts through 4096 on
each endianness, including every count 1..257. It follows every key lane,
checks bounded input/table reads, all tails modulo eight, one reduction per
group, and exactly `80 + 880*n` key-table bytes for `n` complete groups.
There are no assembly stack accesses or 64-bit PMULL instructions, and
all twelve C wrappers call assembly directly.

The QEMU TCG Cortex-A53 guest with KASAN and full crypto self-tests enabled
passes 57 benchmark insertions, 1430 rows and 89,120 timed requests. All
twelve comparison drivers report `selftest: passed`. The eight-block
driver passes every byte length 1..257 with three seeds, block/page/64 KiB
boundaries, SG chunks 1..127 and 1440/1460/4096 bytes, request depths
1/32/128, repeated trials, GCM formatting and unload/reload. The maximum
formatted input is 69,648 bytes. The exact matrices, selected drivers,
byte counts, rates and request totals are independently checked.

Direct shash calls add 13,560 comparisons against `ghash-generic`: 6780
each for `ghash-neon-4x` and `ghash-neon-8x`. These cover 113 lengths from
empty input through 131,201 bytes, four source offsets, three keys,
whole-buffer digest/update/final, and export/import/finup at three splits.
They cover every full-block tail modulo eight and direct calls spanning
one and two internal 64 KiB boundaries. Reference digests, source contents
and guards pass. The guest shuts down without KASAN reports, warnings or
crypto self-test failures. QEMU rates do not establish A53 performance;
big-endian validation remains build/static only. No OpenWrt package/image
was built.

## Thirteen-instruction reducer experiment (release 13)

`ghash-neon-orig-pmull13` compares a shorter byte-PMULL reducer with
`ghash-neon-orig-pmull`, the existing 15-instruction reducer used by the
Linux `ghash-neon-pmull-reduction` branch. It retains the stock loop's
input loads, Karatsuba products, ZIP/EXT reconstruction and 16-byte key
context. This experiment does not use aggregation or the scheduled loop.

The existing reducer forms packed low and high bytes of the products of
each input byte with `0xc2`. Its correction needs only the product of the
first low byte with `0xc2`, placed at bits 112..127. Rotate the low bytes
first, moving that byte to lane 15, then use byte `PMULL2` with a constant
whose only nonzero byte is `0xc2` in lane 15. The correction lands directly
in the required position, removing the per-block zero-vector `MOVI` and
correction-placement `EXT`. Two instructions initialize the additional
constant in `v29` once per assembly call.

The reducer has 13 instructions and five shuffles, versus 15 and six.
The little-endian loop falls from 135 to 133 instructions and still uses
27 byte polynomial multiplies. No input/key loads or spills are added.
Setup costs and dependencies still matter, especially for single-block
calls; this instruction count does not establish a speedup on Cortex-A53.

After installing release 13, unload `ghash_bench` if necessary and reload
`ghash_neon_shuffle`. Benchmark release 2 works unchanged. Check the new
variant's short inputs and fragmented input first:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
insmod ghash_bench drivers=ghash-neon-orig-pmull,ghash-neon-orig-pmull13 \
    lengths=1,2,3,15,16,17,31,32,33,63,64,65,1440,16384,16400,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
insmod ghash_bench drivers=ghash-neon-orig-pmull13 lengths=4097 sg_chunk=17 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
```

Then compare the reducers in both orders, pinned to one CPU. The short
sizes expose the per-call setup cost; 1440 bytes represents a common TCP
MSS, and 16384/16400 bytes cover TLS-sized GHASH inputs:

```sh
for order in ghash-neon-orig-pmull,ghash-neon-orig-pmull13 \
             ghash-neon-orig-pmull13,ghash-neon-orig-pmull; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

The external module builds against Linux 6.18.63 with ARM64 GCC 14 and
`W=1 KCFLAGS=-Werror`; strict checkpatch passes. Little- and big-endian
assembly checks confirm the counts above and unchanged encodings for all
twelve previous functions. Symbolic execution proves equivalence to the
15-instruction reducer, preserved live registers and correct Montgomery
reduction for all 256 product basis bits. C wrappers call assembly directly.

The QEMU TCG Cortex-A53 guest passes 57 benchmark insertions, 1440 rows
and 94,515 timed requests with full crypto self-tests and KASAN enabled.
All thirteen comparison drivers report passed self-tests. Coverage includes
every byte length 1..257 with three seeds, block/page/64 KiB boundaries,
fragmented SGs, depths 1/32/128, GCM formatting and unload/reload. Another
13,560 direct-shash comparisons against `ghash-generic` cover empty input
through 131,201 bytes, four source offsets, three keys, digest/update/final
and export/import/finup. Digests, source contents and guards pass, with no
kernel warnings or KASAN reports. Big-endian validation is build/static
only. QEMU validates correctness, not A53 throughput; no OpenWrt package
or image was built.

### Thirteen-instruction reducer device results

The Cortex-A53 capture at 1.2 GHz passes both driver orders: 140 rows and
73,279,616 timed requests. Each order has five one-second trials per
length, a 100 ms warmup, depth 1, and contiguous raw input. The medians
below pool the ten observations per configuration and are recalculated
from byte counts and elapsed time.

| Input bytes | 15-instruction reducer, Mbit/s | 13-instruction reducer, Mbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 178.881 | 179.369 | +0.27% |
| 32 | 308.557 | 309.503 | +0.31% |
| 64 | 483.560 | 485.494 | +0.40% |
| 1440 | 1055.845 | 1063.351 | +0.71% |
| 16384 | 1112.638 | 1120.742 | +0.73% |
| 16400 | 1112.482 | 1120.672 | +0.74% |
| 65536 | 1114.254 | 1122.378 | +0.73% |

Both orders show the same direction of change. At 65536 bytes, elapsed
time at 1.2 GHz corresponds to 8.616 versus 8.553 nominal cycles per byte,
about one cycle saved per 16-byte block. These are wall-time equivalents,
not PMU cycle measurements. Verification covers preflight, final-slot
digests and source/digest guards; intermediate timed digests are reused.

## PMUL and integer UMULL experiments (release 14)

`ghash-neon-pmul` and `ghash-neon-umull` compare alternative full field
multiplications with `ghash-neon-orig-pmull13`. Each processes one GHASH
block per iteration and uses the same byte-PMULL Montgomery reducer after
assembling the 256-bit product. The new names refer to multiplication;
the final reduction still uses three byte `PMULL`/`PMULL2` instructions.
All thirteen earlier assembly functions retain their instruction encodings.

`PMUL .16b` retains only the low eight bits of each byte product. The PMUL
variant interleaves each operand byte with its bit reversal, so a single
instruction produces both the low product and information about the high
product. If `rbit8` reverses a byte and `pmul8` returns the truncated
carryless product, the missing high byte is:

```text
high = rbit8(pmul8(rbit8(a), rbit8(b))) >> 1
```

The three 64-bit Karatsuba products retain the stock reconstruction.
Even and odd byte rotations accumulate separately; a mask selects the
normal and reversed product bytes before one `RBIT`/shift per 64-bit
product recovers the high bytes. This avoids recovering the high byte
separately for every byte multiply. Key expansion stays outside the loop;
the key context remains 16 bytes. This prototype uses no `TBL` instructions.

`UMULL` and `UMULL2` perform ordinary unsigned integer multiplication,
with two full 32-by-32-bit products per instruction. Two Karatsuba levels
split the 128-bit field product into nine `clmul32` operations. Each uses
the generic implementation's four masks, `0x11111111` through `0x88888888`:
16 integer products, followed by XOR and masks, recover a carryless
32-by-32-bit product. At most eight one-bit terms overlap in each
four-bit column, so carries cannot enter the next retained bit position.

Eight `clmul32` operations run in pairs. The ninth packs the four masks
into vector lanes and uses both `UMULL` and `UMULL2`, avoiding duplicate
work in the spare lane. Key setup precomputes 144 bytes of masked operands,
which the loop reads each block; the complete key context is 160 bytes.
Both assembly loops use fixed memory accesses and length-only branches.

The assembled little-endian loops, including input load, byte-order
conversion, product reconstruction, reduction and loop control, contain:

| Driver | Instructions per 16-byte block | Product multiplies | Reduction multiplies | Key bytes read per block |
| --- | ---: | --- | --- | ---: |
| `ghash-neon-orig-pmull13` | 133 | 24 byte PMULL/PMULL2 | 3 byte PMULL/PMULL2 | 0 |
| `ghash-neon-pmul` | 152 | 24 PMUL | 3 byte PMULL/PMULL2 | 0 |
| `ghash-neon-umull` | 235 | 72 integer UMULL/UMULL2 | 3 byte PMULL/PMULL2 | 144 |

The zero entries exclude key reads during per-call setup. Each big-endian
loop has one fewer instruction, omitting the input `REV64`. There are no
stack spills or helper calls in either new assembly function. Neither
prototype reduces instruction count. The additional masks, XORs and
high-product reconstruction outweigh the changes to the multiplies.
This is a result for these implementations, not a lower bound on all
possible PMUL or integer-multiply algorithms. The device results below
measure their A53 throughput; QEMU TCG timing does not predict it.

Install module release 14 and reload `ghash_neon_shuffle`; `ghash_bench`
release 2 works unchanged. Compare the existing reducer and both new
multiplication variants in both orders:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
for order in ghash-neon-orig-pmull13,ghash-neon-pmul,ghash-neon-umull \
             ghash-neon-umull,ghash-neon-pmul,ghash-neon-orig-pmull13; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

Use `drivers=ghash-neon-pmul,ghash-neon-umull lengths=4097 sg_chunk=17`
with short trials for an additional fragmented-input check. Both variants
share the existing shash buffering, export/import and 64 KiB SIMD-section
limit, and remain below stock priorities. They are experiments; the Linux
`ghash-neon-pmull-reduction` and `ghash-neon-4x` branches are unchanged.

### Release-14 validation

The external module passes ARM64 GCC 14 compilation, modpost and link
against Linux 6.18.63 with `W=1 KCFLAGS=-Werror`, and strict checkpatch.
Both assembly files build for big-endian ARM64 as well. Disassembly
checks confirm the counts above, bounded input/key loads, direct C-wrapper
calls, no stack spills and unchanged encodings for all thirteen controls
on both endiannesses. Checks of the disassembled PMUL product pass all
4096 bilinear basis pairs and 1000 random 64-bit operand pairs. Both UMULL
helpers match independent carryless multiplication for all 1024 bit-basis
pairs, 121 dense edge combinations and 4096 random 32-bit operand pairs.

The QEMU TCG Cortex-A53 guest with KASAN and full crypto self-tests passes
58 benchmark insertions, 2702 rows and 178,476 timed requests. Each new
driver contributes 1276 rows, covering every byte length 1..257 with three
seeds, block/page/64 KiB boundaries, SG chunks 1..127 and 1440/1460/4096,
depths 1/32/128, repeated trials, GCM formatting and module unload/reload.
The largest formatted input is 69,648 bytes. All fifteen comparison
drivers report passed crypto self-tests; exact matrices and byte/rate/
request accounting are checked independently.

Another 18,080 direct-shash comparisons against `ghash-generic` cover
empty input through 131,201 bytes, four source offsets, four keys including
all-ones key/data, digest/update/final and export/import/finup at three
splits. Digest, source and guard checks pass. The guest shuts down without
warnings, crypto self-test failures or KASAN reports. Big-endian validation
is build/static only. No OpenWrt package or image was built, and these
functional checks establish no A53 throughput result.

### PMUL/UMULL device results

The Cortex-A53 device at 1.2 GHz passes 105 rows and reports 48,240,662
timed requests with seed 1708171853, depth 1, contiguous raw input,
1000 ms trials, 100 ms warmup and five runs. This capture contains one
driver order: `orig-pmull13`, `pmul`, then `umull`. Medians calculated
from input byte counts and elapsed time are:

| Input bytes | `orig-pmull13`, Mbit/s | `pmul`, Mbit/s | `umull`, Mbit/s |
| ---: | ---: | ---: | ---: |
| 16 | 178.667 | 173.393 | 142.107 |
| 32 | 307.899 | 292.517 | 212.533 |
| 64 | 484.181 | 444.887 | 282.426 |
| 1440 | 1062.748 | 885.097 | 411.949 |
| 16384 | 1121.004 | 925.055 | 420.320 |
| 16400 | 1120.687 | 925.044 | 420.472 |
| 65536 | 1122.834 | 926.371 | 420.778 |

Every PMUL and UMULL trial is slower than every baseline trial at the
same length. At 64 KiB, throughput falls 17.5% and 62.5%, respectively.
Digest verification covers preflight and final slots, plus guards;
intermediate timed digests are reused. Keep `orig-pmull13` as the
single-block comparison baseline and 4x as the preferred bulk variant.

## Twelve-instruction reducer experiment (release 15)

`ghash-neon-orig-pmull12` retains the original-loop products, input loads,
16-byte key context and the 13-instruction reducer's masked correction.
It combines `UZP1` extraction of the low product bytes and their one-byte
rotation into one two-register `TBL`. The fixed indices are
`2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,0` over the two consecutive
product registers. The final byte still contains the first low product
byte, so multiplying it by the lane-15-only `0xc2` constant places the
second fold's correction directly in bits 112..127.

The reducer drops from 13 to 12 instructions and from five to four
shuffles. The complete little-endian loop drops from 133 to 132
instructions; the big-endian loop has 131. There are still 27 byte
polynomial multiplies per block. Setup adds `ADRP`, `ADD` and one
16-byte constant-table load per assembly call, using otherwise free
`v30`. There are no extra per-block memory accesses, input overreads,
key-context changes or stack spills. The table indices are fixed and
do not depend on input or key values.

The two-register `TBL` and extra setup affect the instruction saving.
Device measurements below show the input-size tradeoff. Install module
release 15 or later and reload `ghash_neon_shuffle`; benchmark release 2
works unchanged:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
insmod ghash_bench drivers=ghash-neon-orig-pmull12 \
    lengths=1,2,3,15,16,17,31,32,33,63,64,65,1440,16384,16400,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
insmod ghash_bench drivers=ghash-neon-orig-pmull12 lengths=4097 sg_chunk=17 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
for order in ghash-neon-orig-pmull13,ghash-neon-orig-pmull12 \
             ghash-neon-orig-pmull12,ghash-neon-orig-pmull13; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

The new driver has priority 84 and must be selected explicitly. All
fifteen earlier implementations remain available with unchanged assembly
instructions. The Linux `ghash-neon-pmull-reduction` and `ghash-neon-4x`
branches are unchanged.

### Release-15 validation

The external module builds against Linux 6.18.63 with ARM64 GCC 14 and
`W=1 KCFLAGS=-Werror`; strict checkpatch passes. Little- and big-endian
assembly checks confirm the instruction counts, the constant-table
contents and relocations, and preservation of the earlier functions.
Symbolic execution of the disassembled reducer proves equivalence to the
13-instruction control for arbitrary inputs, including all 256 product
basis bits, while preserving the live key/mask/permutation registers.
The new update and finup wrappers call assembly directly.

QEMU TCG Cortex-A53 with KASAN and full crypto self-tests passes 58
benchmark insertions, 1,460 rows and 91,510 timed requests. The new driver
contributes 1,266 rows covering every byte length 1..257 with three seeds,
block/page/64 KiB boundaries, fragmented SG input, depths 1/32/128,
repeated trials, GCM formatting and module reload. All sixteen comparison
drivers report passed self-tests. Exact matrices and byte/rate/request
accounting are checked independently.

Another 18,080 direct-shash comparisons against `ghash-generic` cover
both 13- and 12-instruction variants, with 9,040 cases each. They include
empty input through 131,201 bytes, four source offsets, four keys including
all-ones key/data, digest/update/final and export/import/finup at three
splits. Digest, source and guard checks pass, with no kernel warnings or
KASAN reports. Big-endian validation is build/static only. No OpenWrt
package or image was built. These functional checks do not predict A53 throughput.

### Twelve-instruction reducer device results

The 1.2 GHz Cortex-A53 passes 140 rows with 72,946,875 timed requests.
Both driver orders use seed 1708171853, depth 1, contiguous raw input,
1000 ms trials, 100 ms warmup and five runs. Pooled medians from the ten
trials per entry are:

| Input bytes | `orig-pmull13`, Mbit/s | `orig-pmull12`, Mbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 178.347 | 177.351 | -0.56% |
| 32 | 307.878 | 307.158 | -0.23% |
| 64 | 483.518 | 484.183 | +0.14% |
| 1440 | 1062.194 | 1077.020 | +1.40% |
| 16384 | 1120.089 | 1137.098 | +1.52% |
| 16400 | 1120.352 | 1136.809 | +1.47% |
| 65536 | 1122.152 | 1138.990 | +1.50% |

Both orders show a 1.39--1.52% gain at 1440 through 65536 bytes, and
slight regressions at 16/32 bytes consistent with the extra setup cost.
The 64 KiB medians correspond to 8.55 versus 8.43 elapsed cycles per byte
at 1.2 GHz, not PMU cycle measurements. Verification covers preflight and
final-slot digests plus guards; intermediate timed digests are reused.

## Four-block twelve-instruction reducer experiment (release 16)

`ghash-neon-4x-pmull12` compares the same reducer in the existing four-block
loop. It shares `ghash-neon-4x`'s 736-byte key context, setkey function,
products, reconstruction, alternating table order and GPR/D-load schedule.
Calls below four blocks and final one-to-three-block tails still use
`ghash-neon-2x-load`, so the comparison isolates the four-block path.

The four-block products use all 32 vector registers. Keep the reducer's
fixed shuffle indices in `v1`, replacing the old replicated-`0xc2`
constant there. After the products and cross-term join finish, `MOVI`
recreates that multiplier in the now-dead `v3`, and `DUP` copies a
per-call scalar `0xc200000000000000` into both lanes of the now-dead `v4`.
The correction's byte `PMULL2` reads only the upper half of `v4`, where
only byte 15 is nonzero. The lower-half copy is ignored. The reducer
therefore needs no extra permanent vector register, spills or per-group
constant loads, and preserves the cached key table and shuffle registers.

The arithmetic reducer has 12 instructions instead of the existing
four-block path's 15. The two constant instructions reduce the net saving
to one instruction per four-block group:

| Work per four-block group | `4x` | `4x-pmull12` |
| --- | ---: | ---: |
| Reducer, including constant preparation inside the loop | 15 | 14 |
| Forward/reverse instructions, little-endian | 371/376 | 370/375 |
| Forward/reverse instructions, big-endian | 367/372 | 366/371 |
| Byte PMULL/PMULL2 instructions | 99 | 99 |
| Key-table bytes after initial cached table | 400 | 400 |

For calls of four or more blocks, setup adds three instructions and one
16-byte fixed-table load. The early short-call branch bypasses this setup.
No new input or key loads occur inside the loop. The new driver's priority
is 83; select it explicitly. Existing variants and the separate Linux
branches retain their implementations. A53 throughput is not yet measured.

Install module release 16 and reload `ghash_neon_shuffle`; benchmark
release 2 works unchanged. Check group boundaries and fragmented input:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
insmod ghash_bench drivers=ghash-neon-4x,ghash-neon-4x-pmull12 \
    lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,79,80,81,95,96,97,111,112,113,127,128,129,65535,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
for chunk in 47 79 113 1440; do
    insmod ghash_bench drivers=ghash-neon-4x,ghash-neon-4x-pmull12 \
        lengths=4097 sg_chunk="$chunk" depths=1 \
        duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

Then compare both orders pinned to the same CPU:

```sh
for order in ghash-neon-4x,ghash-neon-4x-pmull12 \
             ghash-neon-4x-pmull12,ghash-neon-4x; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,48,64,80,112,128,144,256,1440,16384,16400,65536 \
        depths=1 duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

### Release-16 validation

The external module builds against Linux 6.18.63 with ARM64 GCC 14 and
`W=1 KCFLAGS=-Werror`; strict checkpatch passes. Little- and big-endian
assembly checks confirm that all sixteen earlier implementations retain
their instruction encodings. Symbolic execution proves equivalence of
the new reduction to the existing four-block reducer for arbitrary inputs,
including all 256 product basis bits, and preservation of the live key,
shuffle and constant registers. Both loop directions retain their input
packing, products and memory accesses. Control-flow checks cover 139 block
counts through 4096, including bounds, key lanes at every multiply, the
alternating table order and the short/tail fallback.

QEMU TCG Cortex-A53 with KASAN and full crypto self-tests passes 58
benchmark insertions, 1,470 rows and 83,965 timed requests. The new driver
contributes 1,266 rows covering every byte length 1..257 with three seeds,
block/page/64 KiB boundaries, fragmented input, depths 1/32/128, warmup,
repeated trials, GCM formatting and module reload. All seventeen comparison
drivers pass self-tests. Exact matrices and byte/rate/request accounting
are checked independently.

Another 18,080 direct-shash comparisons against `ghash-generic` cover
`ghash-neon-4x` and `ghash-neon-4x-pmull12`, with 9,040 cases each. They
include empty input through 131,201 bytes, four source offsets, four keys
including all-ones key/data, digest/update/final and export/import/finup
at three splits. Digest, source and guard checks pass, with no kernel
warnings or KASAN reports. Big-endian validation is build/static only.
No OpenWrt package or image was built; QEMU timings are not A53 throughput
measurements.
