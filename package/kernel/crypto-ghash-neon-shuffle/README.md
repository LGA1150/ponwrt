# Standalone ARM64 NEON GHASH comparison drivers

`kmod-crypto-ghash-neon-shuffle` supplies `ghash_neon_shuffle.ko` for a
matching Linux 6.18 kernel. Release 36 registers algorithm `ghash` under 77
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
- **`ghash-neon-4x-pmull13`** tests the 13-instruction reducer with four-block aggregation.
- **`ghash-neon-intrinsics`** translates the original loop and 13-instruction reducer into C.
- **`ghash-neon-orig-direct`** reduces raw Karatsuba products in 15 instructions.
- **`ghash-neon-schoolbook`** multiplies all 128 bits directly without Karatsuba.
- **`ghash-neon-4x-schoolbook`** shares those diagonal shuffles across four products.
- **`ghash-neon-4x-schoolbook-reuse`** alternates traversal of the rotated-key tables.
- **`ghash-neon-4x-schoolbook-load`** also overlaps scalar key loads with arithmetic.
- **`ghash-neon-4x-k32`** adds a 32-bit Karatsuba split to each 64-bit product.
- **`ghash-neon-4x-k32-load`** overlaps that implementation's key loads with SIMD.
- **`ghash-neon-4x-k32-sched`** also interleaves independent reconstruction chains.
- **`ghash-neon-4x-k32-tbl`** replaces reconstruction shifts with fixed permutations.
- **`ghash-neon-4x-k32-tbl-simd`** uses those permutations with direct SIMD key loads.
- **`ghash-neon-4x-k32-tbl-simd-pipe`** moves the next SIMD key load ahead of the final XORs.
- **`ghash-neon-4x-k32-tbl-simd-ldr`** schedules each next key vector after its last use.
- **`ghash-neon-8x-k32-tbl`** combines two four-block K32 groups before reduction.
- **`ghash-neon-8x-k32-merge`** shares inner reconstruction across all eight blocks.
- **`ghash-neon-8x-k32-merge-simd`** uses grouped direct SIMD key loads.
- **`ghash-neon-8x-k32-merge-ldr`** schedules each direct SIMD key load separately.
- **`ghash-neon-8x-k32-merge-early`** moves adjacent scalar-to-SIMD lane writes earlier.
- **`ghash-neon-8x-k32-merge-lanes`** separates the two lane writes with arithmetic.
- **`ghash-neon-8x-k32-early`** tests early adjacent writes on the previous loop.
- **`ghash-neon-8x-k32-lanes`** tests separated lane writes on the previous loop.
- **`ghash-neon-4x-k32-input`** selects input lanes without swapping each input block.
- **`ghash-neon-4x-k32-cross`** reuses inner cross operands for the outer cross product.
- **`ghash-neon-4x-k32-tight`** combines those changes without spills or new key data.
- **`ghash-neon-4x-k32-tight-early`** also prepares each inner cross before its low/high products.
- **`ghash-neon-4x-k32-pipe`** prepares the digest shuffle and reduction constant earlier too.
- **`ghash-neon-16x-k32`** shares field reduction across two eight-block groups without spills.
- **`ghash-neon-16x-k32-merge`** also shares inner reconstruction using 128 bytes of stack scratch.
- **`ghash-neon-8x-k32-no128`** replaces the outer Karatsuba split with direct cross-products.
- **`ghash-neon-8x-k32-no64`** replaces the inner Karatsuba split with direct 32-bit products.
- **`ghash-neon-8x-k32-no128-no64`** removes both splits from the eight-block loop.
- **`ghash-neon-8x-k32-no64-early`** prepares swapped input lanes before the low/high products.
- **`ghash-neon-8x-k32-no128-no64-early`** uses early preparation with both splits removed.
- **`ghash-neon-4x-k32-stp`** stages scalar key loads through stack scratch space.
- **`ghash-neon-4x-k32-stp-pipe`** schedules each SIMD reload separately.
- **`ghash-neon-8x-k32-stp`** tests the scratch path with eight-block aggregation.
- **`ghash-neon-8x-k32-stp-pipe`** combines eight-block aggregation with scheduled reloads.
- **`ghash-neon-4x-gpr-reduce`** performs reduction shifts and XORs in scalar registers.
- **`ghash-neon-4x-stage`** interleaves scalar input preparation and stores with SIMD.
- **`ghash-neon-4x-k16`** adds another Karatsuba split into 16-bit products.
- **`ghash-neon-4x-k16-load`** overlaps that implementation's scalar key loads.
- **`ghash-neon-4x-k8`** splits further to byte products with full Karatsuba.
- **`ghash-neon-4x-k8-load`** overlaps the byte-split implementation's key loads.
- **`ghash-neon-k32`** applies the extra 32-bit Karatsuba split to one block.
- **`ghash-neon-k32-packed`** packs its ninth 32-bit product into two PMULLs.
- **`ghash-neon-k32-sched`** schedules single-block K32 products and reconstruction together.
- **`ghash-neon-k32-u2`** unrolls two serial blocks with the same 16-byte key.
- **`ghash-neon-k32-u4`** unrolls four serial blocks with the same 16-byte key.
- **`ghash-neon-k32-u2-load`** overlaps next-input scalar loads within two-block groups.
- **`ghash-neon-k32-u4-load`** overlaps next-input scalar loads within four-block groups.
- **`ghash-neon-k32-direct`** also feeds Karatsuba products directly to reduction.
- **`ghash-neon-k32-direct-u4-load`** combines direct reduction and four-block preloads.
- **`ghash-neon-k32-packed-sched`** reschedules the packed ninth-product variant.
- **`ghash-neon-k32-direct-stream`** carries preloaded input across unrolled groups.

- **`ghash-neon-4x-k32-compact`** keeps an 80-byte key and builds temporary K32 operands per call.

- **`ghash-neon-4x-k32-stack288`** reconstructs one rotation from a 288-byte temporary table.
- **`ghash-neon-4x-k32-stack144`** reconstructs both rotations from a 144-byte temporary table.
- **`ghash-neon-4x-k32-regs32`** keeps unrotated operands in GPRs with a 32-byte register-save area.
- **`ghash-neon-4x-k32-regs0`** generates inner cross operands using only caller-saved GPRs and no stack.

Each name always selects its own implementation; there is no switching parameter.

The same 77 implementations can also be built and tested entirely in
userspace with [`ghash-bench-userspace`](../../utils/ghash-bench-userspace/README.md).
That tool provides a portable reference, boundary/streaming selftests and
CSV timing without Crypto API or kernel SIMD-entry overhead.

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
84 (orig-pmull12), 83 (4x-pmull12), 82 (4x-pmull13), 81 (intrinsics),
80 (orig-direct), 79 (schoolbook), 78 (4x-schoolbook), 77 (4x-schoolbook-reuse),
76 (4x-schoolbook-load), 75 (4x-k32), 74 (4x-k32-load), 73 (4x-k32-sched),
72 (4x-k32-tbl), 71 (4x-gpr-reduce), 70 (4x-stage), 69 (4x-k16),
68 (4x-k16-load), 67 (4x-k8), 66 (4x-k8-load), 65 (k32),
64 (k32-packed), 63 (4x-k32-tbl-simd), 62 (4x-k32-tbl-simd-pipe),
61 (4x-k32-tbl-simd-ldr), 60 (8x-k32-tbl), 59 (4x-k32-stp),
58 (4x-k32-stp-pipe), 57 (8x-k32-stp), 56 (8x-k32-stp-pipe),
55 (8x-k32-merge), 54 (8x-k32-merge-simd), 53 (8x-k32-merge-ldr),
52 (8x-k32-merge-early), 51 (8x-k32-early), 50 (8x-k32-merge-lanes),
49 (8x-k32-lanes), 48 (8x-k32-no128), 47 (8x-k32-no64),
46 (8x-k32-no128-no64), 45 (8x-k32-no64-early),
44 (8x-k32-no128-no64-early), 43 (16x-k32), 42 (16x-k32-merge),
41 (4x-k32-input), 40 (4x-k32-cross), 39 (4x-k32-tight),
38 (4x-k32-tight-early), 37 (4x-k32-pipe), 36 (k32-sched),
35 (k32-u2), 34 (k32-u4), 33 (k32-u2-load), 32 (k32-u4-load),
31 (k32-direct), 30 (k32-direct-u4-load), 29 (k32-packed-sched),
28 (k32-direct-stream), 27 (4x-k32-compact), 26 (4x-k32-stack288),
25 (4x-k32-stack144), 24 (4x-k32-regs32) and 23 (4x-k32-regs0) are below stock
`ghash-neon` (150) and `ghash-generic` (100), so
stock implementations win selection by `ghash`. Select the experimental drivers explicitly.
Crypto API module aliases cover only these 77 unique names. The package has no
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
branches retain their implementations. Device results below show a small
bulk gain and short-input regressions.

Install module release 16 or later and reload `ghash_neon_shuffle`; benchmark
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

### Four-block twelve-instruction reducer device results

Both driver orders pass on the 1.2 GHz Cortex-A53: 260 rows and
176,973,889 timed requests. The capture uses seed 1708171853, depth 1,
contiguous raw input, 1000 ms trials, 100 ms warmup and five runs per order.
Pooled medians from ten trials per driver are:

| Input bytes | `4x`, Mbit/s | `4x-pmull12`, Mbit/s | Change |
| ---: | ---: | ---: | ---: |
| 16 | 173.300 | 173.017 | -0.16% |
| 32 | 321.518 | 321.597 | +0.02% |
| 48 | 405.208 | 404.917 | -0.07% |
| 64 | 566.900 | 565.787 | -0.20% |
| 80 | 605.376 | 603.777 | -0.26% |
| 112 | 708.381 | 707.334 | -0.15% |
| 128 | 866.128 | 865.870 | -0.03% |
| 144 | 863.940 | 861.368 | -0.30% |
| 256 | 1178.034 | 1178.575 | +0.05% |
| 1440 | 1648.892 | 1656.752 | +0.48% |
| 16384 | 1823.008 | 1828.971 | +0.33% |
| 16400 | 1821.150 | 1826.182 | +0.28% |
| 65536 | 1823.094 | 1828.988 | +0.32% |

Every new-variant trial exceeds every baseline trial at the same length
from 1440 bytes upward. The separate orders gain 0.20% and 0.48% at
1440 bytes, and 0.26--0.34% at 16--64 KiB. The bulk gain is small,
consistent with saving one instruction per roughly 370-instruction group.
Extra setup can outweigh that saving for short input. Inputs of 16/32/48
bytes use the same tail implementation, so their differences are not from
the reducer. Verification covers preflight/final-slot digests and guards;
intermediate timed digests are reused.

## Four-block thirteen-instruction reducer experiment (release 17)

`ghash-neon-4x-pmull13` compares the masked 13-instruction reducer with
both `ghash-neon-4x` and `ghash-neon-4x-pmull12`. It keeps the replicated
`0xc2` multiplier in `v1` throughout the product loop, as the original
four-block variant does. After the products and cross-term join finish,
one `SHL v4.2d, v1.2d, #56` keeps only the lowest byte of each 64-bit
lane and moves it to the highest byte. Only the upper half feeds the
correction's byte `PMULL2`, making byte 15 its sole active multiplier byte.
This uses dead `v4` and needs no scalar register or extra setup. `UZP1`
followed by `EXT` extracts and rotates the low product bytes; there is no
additional shuffle-table load.

The 13 reducer instructions plus one constant-preparation instruction
give the same 14 instructions per group as `4x-pmull12`. Setup is identical
to the original four-block loop.
Compared with `4x-pmull12`, this removes the per-group replicated-constant
`MOVI`, uses `SHL` instead of `DUP`, replaces the reducer's two-register
`TBL` with `UZP1`/`EXT`, and removes three setup instructions and the
16-byte table load.

| Work | `4x` | `4x-pmull12` | `4x-pmull13` |
| --- | ---: | ---: | ---: |
| Reduction instructions including per-group constant preparation | 15 | 14 | 14 |
| Forward/reverse instructions, little-endian | 371/376 | 370/375 | 370/375 |
| Forward/reverse instructions, big-endian | 367/372 | 366/371 | 366/371 |
| Extra setup instructions per call of four or more blocks | 0 | 3 | 0 |
| Extra setup bytes loaded | 0 | 16 | 0 |
| Byte PMULL/PMULL2 instructions per group | 99 | 99 | 99 |

The key context/setkey, products, input packing, alternating table order
and bounded short/tail path are unchanged. No new spills or input/key
loads are introduced. Earlier implementations retain their assembly
encodings, and the new driver's priority 82 keeps selection explicit.
Device performance remains to be measured.

Install comparison module release 17, reload `ghash_neon_shuffle`, and
use the unchanged benchmark release 2. First check boundaries and SG input:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
insmod ghash_bench drivers=ghash-neon-4x-pmull13 \
    lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,79,80,81,95,96,97,111,112,113,127,128,129,65535,65536 \
    depths=1,32 duration_ms=10 warmup_ms=0 runs=1
rmmod ghash_bench
for chunk in 47 79 113 1440; do
    insmod ghash_bench drivers=ghash-neon-4x-pmull13 \
        lengths=4097 sg_chunk="$chunk" depths=1 \
        duration_ms=10 warmup_ms=0 runs=1 || break
    rmmod ghash_bench || break
done
```

Then compare the three four-block variants in both orders:

```sh
for order in ghash-neon-4x,ghash-neon-4x-pmull12,ghash-neon-4x-pmull13 \
             ghash-neon-4x-pmull13,ghash-neon-4x-pmull12,ghash-neon-4x; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,48,64,80,112,128,144,256,1440,16384,16400,65536 \
        depths=1 duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

### Release-17 validation

The external module builds against Linux 6.18.63 with ARM64 GCC 14 and
`W=1 KCFLAGS=-Werror`; strict checkpatch passes. Little- and big-endian
assembly checks confirm identical setup to the original four-block loop,
370/375 and 366/371 instructions in the respective forward/reverse bodies,
and no additional loads or spills. All seventeen earlier implementations
retain their assembly encodings. Symbolic checks prove the reducer for
arbitrary inputs and all 256 product basis bits, the four-term products,
and preservation of cached keys and shuffle constants. Control-flow checks
cover 139 block counts through 4096, including input/key bounds and tails.

QEMU TCG Cortex-A53 with KASAN and full crypto self-tests passes 58
benchmark insertions, 1,504 rows and 75,173 timed requests. The new driver contributes 1,266
rows covering every byte length 1..257 with three seeds, block/page/64 KiB
boundaries, fragmented input, depths 1/32/128, warmup, repeated trials,
GCM formatting and module reload. All eighteen comparison drivers pass
self-tests. Exact matrices and byte/rate/request accounting are checked
independently.

Another 18,080 direct-shash comparisons against `ghash-generic` cover
`ghash-neon-4x-pmull12` and `ghash-neon-4x-pmull13`, with 9,040 cases each.
They include empty input through 131,201 bytes, four source offsets, four
keys including all-ones key/data, digest/update/final and export/import/finup
at three splits. Digest, source and guard checks pass, with no kernel
warnings or KASAN reports. Big-endian validation is build/static only.
No OpenWrt package or image was built; QEMU timings are not A53 throughput
measurements.

## C NEON intrinsics experiment (release 18)

`ghash-neon-intrinsics` translates the single-block algorithm used by
`ghash-neon-orig-pmull13` into C through `<asm/neon-intrinsics.h>`. It keeps
three 64-bit Karatsuba products, their eight byte-PMULL expansions, and the
masked byte-PMULL reducer. Fixed rotations, lane interleaving and XORs use
NEON intrinsics; no inline assembly or optional 64-bit PMULL instruction
is needed. The original 16-byte key context, frontend buffering, export/import
and 64 KiB SIMD-section limit are shared with the assembly control.

Only the new translation unit receives `CC_FLAGS_FPU` and removes
`CC_FLAGS_NO_FPU`. The ordinary C frontend continues to use general-purpose
registers and calls the new function inside its existing kernel SIMD section.
Permutation constants are loaded from byte arrays, and masks are constructed
with lane intrinsics: C vector initializer ordering on big-endian GCC differs
from intrinsic lane numbering. No secret-dependent indices or branches are
introduced.

The compiler chooses the instruction order and register allocation. With
ARM64 GCC 14.2, the non-KASAN little-endian loop has **147 instructions**
versus **133** for `ghash-neon-orig-pmull13`, with 27 byte polynomial multiplies
in each. The C loop loads six constant vectors and one spilled 64-bit key
rotation per block, in addition to the input block; the assembly loop only
loads input. The C function also saves/restores `d8` through `d15` to obey
the C calling convention. GCC combines the reducer's `UZP1` and `EXT` into
a two-register `TBL`, so writing the 13-instruction algorithm in intrinsics
does not preserve its exact instruction sequence. These are compiler-output
observations, not device throughput measurements.

After installing comparison module release 18, reload it and compare both
orders using the unchanged benchmark release 2:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
for order in ghash-neon-orig-pmull13,ghash-neon-intrinsics \
             ghash-neon-intrinsics,ghash-neon-orig-pmull13; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

### Release-18 validation

ARM64 GCC 14 builds the external module against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`; strict checkpatch passes. QEMU TCG Cortex-A53 with
KASAN and full crypto self-tests passes 58 benchmark insertions, 1,514 rows
and 77,494 timed requests, including 1,266 rows for the new driver. Coverage
includes every byte length 1..257 with three seeds, partial/fragmented input,
MSS and 64 KiB boundaries, GCM formatting, depths 1/32/128 and module reload.
All nineteen drivers pass crypto self-tests. Another 18,080 direct-shash
comparisons against `ghash-generic` cover the assembly control and C variant,
including empty input through 131,201 bytes, four keys and source offsets,
streaming, and export/import. Digest, input and guard checks pass without
kernel warnings or KASAN reports. The final constant-initialization changes
preserve the tested little-endian machine code and constant bytes.

Separate bare-metal QEMU runs compare the C and assembly functions in both
endian modes: 17,536 cases per mode cover all 128-by-128 key/input basis pairs
and randomized keys, accumulators and input through 4,096 blocks, including
unaligned loads. This checks the arithmetic and lane layout; it does not claim
support for a big-endian Linux configuration, which already depends on
`BROKEN` in Linux 6.18. No OpenWrt package/image was built, and QEMU timings
are not A53 performance measurements.

## Direct reduction of Karatsuba products (release 19)

`ghash-neon-orig-direct` reduces the raw `XL`, `XM` and `XH` products after
the original loop's final multiplication. It combines reconstruction and
reduction in **15 instructions**. The `ghash-neon-orig-pmull13` control takes
**16 instructions at this point**: three caller instructions followed by
its 13-instruction reducer, which includes the remaining reconstruction.
These totals measure the same work from the same input registers.

Let `S = XL XOR XH` and `C = XM XOR S`, where `XM` is the raw Karatsuba
cross product. For the reconstructed 256-bit product `H:L`:

```text
L.low  = XL.low
L.high = XL.high XOR C.low
H XOR L = S XOR swap64(C)
```

The two byte-PMULL operations multiply `XL.low` and
`(EXT(XL, XH, 8) XOR C).low` directly by `0xc2`. Another `EXT` swaps the
halves of `C` to form `H XOR L` without packing `L` or `H`. This removes
two `ZIP` instructions and adds one `EXT`; the final seven reduction
instructions are unchanged. The result stays in `XL` for the next block.

The full little-endian loop has **132 instructions versus 133**, with
27 byte polynomial multiplies in both. Setup, key size, input loads,
register usage and frontend buffering are unchanged, with no added loads
or spills. Instruction count alone does not establish A53 throughput.
The existing variants remain available for comparison. Release 19 also
fixes package staging to copy the header required by the C intrinsics variant.

After installing comparison module release 19, reload it and compare both
orders using the existing benchmark:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
for order in ghash-neon-orig-pmull13,ghash-neon-orig-direct \
             ghash-neon-orig-direct,ghash-neon-orig-pmull13; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

### Release-19 validation

ARM64 GCC 14 builds the staged external module against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`. Symbolic execution of the assembled reducer matches
the control for arbitrary raw products; an independent field-polynomial
check covers all 384 `XL/XM/XH` basis bits. Live keys and constants are
preserved. All nineteen previous block functions retain their instruction
encodings, and the frontend uses direct calls to the new function.

Strict checkpatch passes. QEMU TCG Cortex-A53 with KASAN and full crypto
self-tests passes 58 benchmark insertions, 1,524 rows and 83,170 timed
requests. The new driver contributes 1,266 rows covering every byte length
1..257 with three seeds, fragmented and partial input, MSS/page/64 KiB
boundaries, GCM formatting, depths 1/32/128, warmup and module reload.
All twenty comparison drivers pass crypto self-tests. Exact matrices,
driver selection and byte/rate/request accounting are checked independently.

The control and direct variant each pass 9,040 direct-shash comparisons
against `ghash-generic`: empty input through 131,201 bytes, four keys,
four source offsets, digest/update/final and export/import/finup at three
splits. Digests, unchanged input and guards pass without kernel warnings
or KASAN reports. No OpenWrt package/image was built. Subsequent A53
comparisons with schoolbook multiplication are recorded below.

## Direct schoolbook multiplication (release 20)

`ghash-neon-schoolbook` tests a non-Karatsuba 128-by-128-bit product. It
uses 32 byte-PMULL instructions to generate all 256 byte-pair products
and accumulates them directly into the low and high 128-bit product halves.
It shares the existing frontend, 16-byte key context, partial-block buffering,
export/import and 64 KiB SIMD-section limit with the original-loop controls.

For input bytes `a[j]` and key bytes `b[j]`, rotation distances `d=1..7`
pair the symmetric products

```text
a[j] * b[(j+d) mod 16] XOR a[(j+d) mod 16] * b[j]
```

Four byte-PMULL instructions cover each distance: two directions, each
covering eight lanes in the low and high input halves. Two more instructions
cover distance zero, and two cover distance eight, for 32 in total.
Distances zero and eight need no symmetric-pair duplication.

Circular rotations wrap the last `d` input bytes. Their products belong
16 bytes lower in the final polynomial than an uncorrected diagonal shift
would place them. `.Lschoolbook_masks` selects the last `d` 16-bit lanes
of the high product vector; XORing the selected bits into both vectors
moves those contributions into the low half before the diagonal shift.
The seven masks occupy 112 bytes and are loaded once per block-function
call. Eight key rotations and both reduction constants are also prepared
once and retained in registers through the loop.

The assembled little-endian loops have these counts, including input
handling and the loop branch. PMULL counts include final reduction:

| Driver | Instructions/block | Byte PMULL/PMULL2 |
| --- | ---: | ---: |
| `ghash-neon-orig-pmull13` | 133 | 27 |
| `ghash-neon-orig-direct` | 132 | 27 |
| `ghash-neon-schoolbook` | 123 | 35 |

Compared with `ghash-neon-orig-pmull13`, the new loop removes 26 permutation
instructions and adds eight PMULLs, seven XORs and one AND. It has no
inner-loop constant loads or spills. The 256-bit product feeds the same
byte-PMULL Montgomery reduction, with no Karatsuba reconstruction needed.
These instruction counts do not establish throughput: additional PMULLs
consume issue capacity, and pairing, dependencies and setup costs matter.

After installing comparison module release 20, reload it and measure the
three single-block variants in both orders using the existing benchmark:

```sh
rmmod ghash_neon_shuffle
modprobe ghash_neon_shuffle
for order in ghash-neon-orig-pmull13,ghash-neon-orig-direct,ghash-neon-schoolbook \
             ghash-neon-schoolbook,ghash-neon-orig-direct,ghash-neon-orig-pmull13; do
    taskset -c 0 insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 depths=1 \
        duration_ms=1000 warmup_ms=100 runs=5 || break
    rmmod ghash_bench || break
done
```

### Release-20 validation

ARM64 GCC 14 builds the staged external module against Linux 6.18.63 with
`W=1 KCFLAGS=-Werror`. Strict checkpatch passes with the kernel-only
new-file `MAINTAINERS` reminder excluded for this package source.
Symbolic execution of the assembled product and reduction passes all
16,384 input/key basis pairs, with live keys, masks and constants preserved.
The 112-byte mask table matches the diagonal placement rules. The new
loop retains the proven prototype's instruction encodings; all twenty
previous block functions retain theirs. The frontend calls the new
function directly.

QEMU TCG Cortex-A53 with KASAN and full crypto self-tests passes 58
benchmark insertions, 1,534 rows and 82,971 timed requests. The new driver
contributes 1,266 rows covering every byte length 1..257 with three seeds,
fragmented/partial input, MSS/page/64 KiB boundaries, GCM formatting,
depths 1/32/128, warmup and module reload. All twenty-one comparison drivers
pass crypto self-tests. Exact matrices, driver selection and
byte/rate/request accounting are checked independently.

The schoolbook and original-loop PMULL controls each pass 9,040 direct-shash
comparisons against `ghash-generic`, including empty input through 131,201
bytes, four keys, four source offsets, digest/update/final and
export/import/finup at three splits. Digests, unchanged input and guards
pass without kernel warnings or KASAN reports. No OpenWrt package/image
was built; QEMU timings are not device performance measurements.

### Device comparison: original, direct and schoolbook

The supplied Cortex-A53 capture at the reported 1.2 GHz clock passes both
driver orders: 210 timed rows and 109,934,500 requests, hashing
20,594,146,928 bytes (19.180 GiB). Each driver/length pair has ten trials:
five in each order, with one-second measurements, 100 ms warmup, depth one,
contiguous raw input and seed 1708171853. Requested and resolved drivers,
the exact matrices, byte/request totals and throughput accounting agree.
All asynchronous and backlog counts are zero. The benchmark's preflight,
final-slot digest and guard checks pass; intermediate timed digests are reused.

Pooled medians below are Gbps, calculated from the byte counts and elapsed
times rather than the rounded printed rates. `13-inst` denotes
`ghash-neon-orig-pmull13`, and `direct` denotes `ghash-neon-orig-direct`.

| Bytes | 13-inst | Direct | Schoolbook | Direct vs 13-inst | Schoolbook vs 13-inst |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.178314 | 0.178890 | 0.178170 | +0.32% | -0.08% |
| 32 | 0.307232 | 0.309716 | 0.309433 | +0.81% | +0.72% |
| 64 | 0.482537 | 0.487507 | 0.489946 | +1.03% | +1.54% |
| 1440 | 1.061216 | 1.083732 | 1.106842 | +2.12% | +4.30% |
| 16384 | 1.118913 | 1.143919 | 1.169535 | +2.23% | +4.52% |
| 16400 | 1.118511 | 1.143726 | 1.169663 | +2.25% | +4.57% |
| 65536 | 1.120294 | 1.145181 | 1.171366 | +2.22% | +4.56% |

Both orders retain the same ranking. Every direct trial exceeds every
13-instruction-control trial at the same length. Schoolbook beats both
controls on every trial at each length from 64 bytes upward; at 1440 bytes
and above it exceeds direct by 2.13-2.29%. Direct is fastest at 16 and 32
bytes, where schoolbook is respectively 0.40% and 0.09% slower. Schoolbook
and the 13-instruction control have overlapping trial ranges at 16 bytes.

At 64 KiB, elapsed throughput corresponds to approximately 137.11,
134.13 and 131.13 cycles per 16-byte block for 13-inst, direct and
schoolbook, using the reported 1.2 GHz clock. These are elapsed-time
equivalents including benchmark/API overhead, not PMU cycle measurements
of the assembly loops. The results show that the schoolbook tradeoff pays
off for larger buffers on this A53 despite its eight additional byte
PMULLs. They do not identify individual pipeline stalls or establish A55
performance; this device capture covers the listed aligned raw-input cases.

## Four-block schoolbook multiplication (release 21)

`ghash-neon-4x-schoolbook` evaluates

```text
Y' = (Y XOR X1) * H^4 XOR X2 * H^3 XOR X3 * H^2 XOR X4 * H
```

with four direct 128-by-128-bit schoolbook products and one reduction.
For each byte-distance diagonal, it XORs the four products' raw byte-PMULL
results before applying the wrap mask and placing the diagonal in the
256-bit sum. The seven wrap corrections and their shifts run once per
four-block group. No intermediate 64-by-64 multiplication or Karatsuba
cross term is formed.

Key setup computes H through H^4 using `gf128mul_lle`, reflects each
power and stores its full 128-bit byte rotations 0 through 8. The four
unrotated powers stay in registers throughout the loop; each group loads
the other eight sets of rotations. The seven masks, zero vector and two
reduction constants also stay in registers. The result is a single loop
with no stack spills, input rereads or alternating table traversal.

The 592-byte key context begins with the single-block H representation.
Calls shorter than four blocks and final groups of one to three blocks
use `ghash-neon-schoolbook` directly. This differs from the older 4x
drivers' two-block tail path, so the comparison includes short lengths
and 1440/16400-byte buffers as well as multiples of 64 bytes. Frontend
buffering, state export/import and the 64 KiB SIMD-section limit are
unchanged. The earlier 21 block functions retain their instruction
encodings; the new driver is selected explicitly at priority 78.

Counts from the non-KASAN ARM64 GCC 14.2 build, for little-endian input:

| Work per four-block group | `4x-pmull13` | `4x-schoolbook` |
| --- | ---: | ---: |
| Instructions, including loop control | 370/375, alternating | 352 |
| Byte PMULL/PMULL2 instructions | 99 | 131 |
| Final reduction instructions | 14, including constant preparation | 10 |
| Key context bytes | 736 | 592 |

Schoolbook saves shuffle/reconstruction work but adds 32 byte polynomial
multiplies and loads 512 bytes of rotated keys per group. Fewer total
instructions do not establish a throughput improvement on either A53 or
A55. Measure on the device; QEMU is used for correctness only.

After installing release 21 and reloading `ghash_neon_shuffle`, the
unchanged `ghash_bench` module can check short calls and all four-block
remainders:

```sh
insmod ghash_bench drivers=ghash-neon-4x-schoolbook \
    lengths=1,15,16,17,31,32,33,47,48,49,63,64,65,79,80,95,96,111,112,127,128,129 \
    depths=1 duration_ms=10 warmup_ms=0 runs=1 seed=1708171853 &&
    rmmod ghash_bench
```

Compare single-block schoolbook, the original 4x loop, the 4x reducer with
13 instructions and the new 4x schoolbook loop in both orders:

```sh
for order in ghash-neon-schoolbook,ghash-neon-4x,ghash-neon-4x-pmull13,ghash-neon-4x-schoolbook \
             ghash-neon-4x-schoolbook,ghash-neon-4x-pmull13,ghash-neon-4x,ghash-neon-schoolbook; do
    insmod ghash_bench drivers="$order" \
        lengths=16,32,64,1440,16384,16400,65536 \
        depths=1 duration_ms=1000 warmup_ms=100 runs=5 seed=1708171853 || break
    rmmod ghash_bench || break
done
```

This measures raw, contiguous input. SG timing remains a separate check:

```sh
for chunk in 15 17 1440 1460; do
    insmod ghash_bench drivers=ghash-neon-4x,ghash-neon-4x-schoolbook \
        lengths=63,64,65,127,128,129,1440 \
        sg_chunk="$chunk" depths=1 duration_ms=10 warmup_ms=0 runs=1 \
        seed=1708171853 || break
    rmmod ghash_bench || break
done
```

### Validation

Direct external-module builds against prepared Linux 6.18.63 ARM64 kernels,
with and without KASAN, pass GCC 14.2 `W=1 KCFLAGS=-Werror`. Strict
checkpatch reports no errors, warnings or checks. Static evaluation of
the assembled code verifies all 65,536 corresponding input/key basis
pairs across the four products, the absence of cross-slot terms and the
shared Montgomery reduction. It also checks live registers, input/key
read bounds, short/tail branches and preservation of all earlier block
functions.

Cortex-A53 QEMU TCG with KASAN and full crypto tests enabled passes 58
benchmark invocations, 1,544 rows and 87,688 timed requests. The new driver
accounts for 1,266 rows: every input length 1 through 257 under three
seeds, block/page/64 KiB boundaries, fragmented SGs, GCM-formatted input,
depths 1/32/128, warmup/repeated runs and module reload. These rows check
digests before timing and after the final slot use; intermediate timed
digests are not all individually checked. All 22 comparison drivers
report passed Crypto API selftests.

The direct-shash fixture passes another 18,080 generic-reference comparisons,
9,040 each for `ghash-neon-4x-pmull13` and the new driver. It covers zero
through 131,201 input bytes, four key/data patterns, input offsets 0/1/7/15,
digest and update/final calls, export/import at three splits, and source
and output guards. No kernel warnings or KASAN reports occur. No OpenWrt
package or image was built. QEMU validates correctness; device throughput
results follow below.

### A53 device comparison: schoolbook with four-block aggregation

The release-21 capture on the user's 1.2 GHz Cortex-A53 passes all 140
rows and 75,792,220 timed requests (17,871,167,440 payload bytes,
16.644 GiB). It contains one order: single-block schoolbook, original 4x,
4x-pmull13, then 4x-schoolbook. The reverse order is not included.
All requested drivers resolve exactly, asynchronous/backlog counts are
zero, and the CSV byte totals and rates agree. Verification covers
preflight, final slots and guards, not every intermediate timed digest.

Each driver/length has five one-second trials with a 100 ms warmup,
seed 1708171853, 128-byte alignment, contiguous raw input and depth one.
Medians below are calculated from bytes and elapsed time, in Gbit/s:

| Input bytes | Schoolbook 1x | Original 4x | 4x-pmull13 | Schoolbook 4x | Schoolbook 4x vs original 4x |
| --- | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.177628 | 0.171918 | 0.172620 | 0.175719 | +2.21% |
| 32 | 0.308707 | 0.320069 | 0.321162 | 0.305776 | -4.47% |
| 64 | 0.488931 | 0.564695 | 0.567132 | 0.527931 | -6.51% |
| 1440 | 1.104801 | 1.648904 | 1.651114 | 1.337116 | -18.91% |
| 16384 | 1.168188 | 1.817325 | 1.819917 | 1.444211 | -20.53% |
| 16400 | 1.167951 | 1.814896 | 1.814596 | 1.448205 | -20.20% |
| 65536 | 1.169743 | 1.819200 | 1.817362 | 1.449464 | -20.32% |

Aggregation improves schoolbook's 64 KiB median by 23.91% over its
single-block loop, but it is 20.32% below original 4x and 20.24% below
4x-pmull13. At every tested length from 64 bytes upward, every new 4x
schoolbook trial is slower than every trial of both existing 4x variants,
while exceeding every single-block schoolbook trial. At 16 and 32 bytes
the new driver uses its single-block tail, so those rows do not measure
four-block multiplication. The existing 4x variants retain the faster
two-block path for 32-byte calls.

The instruction-count reduction did not translate into faster execution.
The new loop adds 32 byte PMULLs per group and loads 512 bytes of rotated
keys, versus 400 bytes in steady state for the existing 4x loop. The
latter also overlaps scalar/GPR and D-register key loads with polynomial
multiplies; the new loop loads four key vectors together before each
diagonal. Multiply issue cost, dependencies and load scheduling are
plausible costs, but this capture does not separate them. At 64 KiB the
elapsed rates correspond to about 337.73 cycles per four blocks for
original 4x and 423.88 for schoolbook 4x at 1.2 GHz, including API/benchmark
overhead; these are not PMU cycle counts.

Retain the existing Karatsuba 4x implementation as the preferred bulk
variant on this A53. The large regression has no overlapping trial
ranges in this capture, although a reverse-order run remains useful for
checking order effects. It does not establish A55 performance or negate
the earlier single-block schoolbook improvement.

## Schoolbook table traversal and load scheduling (release 22)

Two explicitly selected variants separate table reuse from load scheduling:

- `ghash-neon-4x-schoolbook-reuse` visits rotations 1 through 8, then 8
  through 1, keeping the last four rotated keys in registers for the next
  group. The unrotated powers stay in v4-v7. Both traversal directions
  accumulate the same four products and reduce once per 64 input bytes.
- `ghash-neon-4x-schoolbook-load` uses the same traversal but reads the next
  four keys with eight scalar `LDR X` instructions interleaved with the
  current arithmetic. Each old key is replaced with `FMOV`/`INS` only after
  its final use. These instructions and their register-file transfer costs
  remain part of the measured loop.

Both use the existing 592-byte context, masks, reduction, and single-block
schoolbook fallback for short calls and tails. They add neither stack spills
nor input rereads. The original schoolbook functions retain their machine
code. Their priorities are 77 and 76, below the earlier comparisons; select
them by exact driver name. Userspace benchmark release 2 compiles these same
functions without installing or loading the comparison module.

The two steps are unrolled into a 128-byte loop, with two reductions, like
the existing Karatsuba traversal. For n complete four-block groups, rotated
key reads are `64 + 448*n` bytes, compared with `512*n` for the original
schoolbook loop. The initial preload means a single group saves no reads.

| Work per four-block group | Schoolbook | Reuse | Reuse + scalar loads | Karatsuba PMULL13 |
| --- | ---: | ---: | ---: | ---: |
| Instructions including loop control | 352 | 350/352 | 462/464 | 370/375 |
| Byte PMULL/PMULL2 instructions | 131 | 131 | 131 | 99 |
| Steady-state rotated-key bytes | 512 | 448 | 448 | 400 |

Instruction counts are from the little-endian assembly. Alternating counts
refer to the forward and reverse steps. Scalar loads can pair with SIMD
arithmetic on this A53, but the SIMD transfers still consume execution
resources; increasing retired-instruction IPC is not itself a speedup.

### Native Cortex-A53 measurements

The userspace executable ran on CPU3 at 1.2 GHz with the performance governor,
seed 1708171853, aligned input, 100 ms warmup, and five 300 ms trials in each
of the forward and reverse algorithm/size orders. These medians pool ten
samples per cell. All 280 timed rows passed preflight/final-digest and guard
verification; intermediate timed digests are not individually retained.

| Input bytes | Schoolbook | Reuse | Reuse + scalar loads | Karatsuba PMULL13 |
| --- | ---: | ---: | ---: | ---: |
| 64 | 1.320989 | 1.310595 | 1.316468 | 1.585587 |
| 128 | 1.384788 | 1.394595 | 1.402216 | 1.699805 |
| 256 | 1.420972 | 1.438978 | 1.446380 | 1.766408 |
| 1440 | 1.437596 | 1.461992 | 1.469275 | 1.802815 |
| 16384 | 1.456967 | 1.485283 | 1.491426 | 1.836067 |
| 16400 | 1.455835 | 1.483093 | 1.490503 | 1.834387 |
| 65536 | 1.450577 | 1.479626 | 1.487280 | 1.828009 |

Rates are payload Gbit/s. At 64 KiB, traversal reuse gains 2.00%; scalar-load
scheduling adds another 0.52%, for 2.53% over the original schoolbook loop.
The resulting loop remains 18.64% below Karatsuba. Every Karatsuba trial
exceeds every schoolbook trial at the same tested length. A 64-byte call
cannot reuse a table across groups and is slightly slower with the new
setup. These are userspace measurements, not Crypto API or kTLS rates.

Eight additional 64-KiB runs, one per algorithm in each order, collected
userspace-only perf counters as a single non-multiplexed group. Intervals
ending at approximately two and three seconds exclude startup/warmup and
the final partial interval. Each run used a 100 ms warmup and a three-second
trial. PMU ratios pool those four complete intervals per algorithm:

| Variant | Retired IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| Schoolbook | 0.8341 | 1.8959% | 0.0010% | 0.3963% |
| Reuse | 0.8493 | 1.9358% | 0.0013% | 0.2723% |
| Reuse + scalar loads | 1.1245 | 1.2145% | 0.1227% | 0.3745% |
| Karatsuba PMULL13 | 1.1116 | 1.0452% | 0.0015% | 0.5476% |

Raw A53 events are 0xe6, 0xe5 and 0xe7, respectively. These events need not
be mutually exclusive and do not account for every execution-resource
restriction. The improved schoolbook loop now has higher IPC than the
Karatsuba loop, but still executes substantially more instructions per
group. Alternating traversal and this load schedule therefore explain
only a small part of the original throughput difference. This does not
rule out a better schoolbook schedule or establish A55 performance.

### Validation

GCC 14.2 builds the userspace executable and the external ARM64 module with
warnings treated as errors (`W=1 KCFLAGS=-Werror` for the module). Strict
checkpatch passes. User-mode Cortex-A53 QEMU TCG passes 10,080 full selftest
comparisons per new variant, including continuing state, input offsets
0/1/7/15, four key/data patterns, protected input boundaries and lengths
through 131,201 bytes. Native CPU3 passes another 40,320 comparisons across
the two new variants and the schoolbook/Karatsuba controls, including known
answers and AAPCS64 register preservation. Raw CSV accounting, capture
hashes, exact test matrices and PMU scheduling are checked separately.

No OpenWrt package/image was built and no kernel module was loaded for
this comparison. The native results exercise the shared assembly directly;
new driver registration has build coverage. All remote test files remain
under `/tmp/ghash-test`.

## Extra Karatsuba, scalar reduction and stores (release 23)

Six additional drivers compare an extra Karatsuba split, scalar reduction,
and scalar input preparation with interleaved stores. All are also selectable
in userspace benchmark release 3. Existing driver priorities are unchanged;
these are explicit comparison targets.

### Multiplication and scheduling

The `4x-k32` family keeps the outer 128-bit Karatsuba split and splits each
64-bit product into 32-bit halves. For operands `a0 + a1*x^32` and
`b0 + b1*x^32`, it computes `a0*b0`, `a1*b1`, and
`(a0 XOR a1)*(b0 XOR b1)`. These three 32-bit products reconstruct the
64-bit product. Byte PMULL/PMULL2 constructs each 32-bit product from its
diagonals; sums over the four GHASH terms remain unreconstructed until the
last term. This reduces the multiply/reduce count from 99 to 75 byte
polynomial instructions per 64 input bytes.

The 688-byte key context contains the unchanged 256-byte two-block fallback
context and 432 bytes of rotated low/high/cross operands. Calls shorter than
four blocks and one-to-three-block tails use `ghash-neon-2x-load`. There are
no input overreads or data-dependent table lookups.

- `4x-k32` loads each group of keys with `LD1`.
- `4x-k32-load` keeps those vectors live while loading the next group through
  six scalar registers, then transfers them with `FMOV`/`INS`. The next-table
  prefetch wraps within the key context, including on the final iteration.
- `4x-k32-sched` interleaves the independent low/high and cross-product
  reconstruction chains, increasing spacing between dependent SIMD operations.
- `4x-k32-tbl` also replaces shift pairs and cross-term placement with fixed
  `TBL` permutations. It saves eight instructions per group, including the
  extra instruction used to form the reducer's correction constant.

The other two experiments retain the existing four-block multiplication:

- `4x-gpr-reduce` extracts the reconstructed product into four scalar words,
  uses shifted-operand `EOR` instructions for reduction, and transfers the
  result back into SIMD. This removes the three reduction PMULLs but adds
  register transfers and a scalar dependency chain.
- `4x-stage` prepares the next input group with scalar loads, byte reversals,
  XORs and six `STP` instructions interleaved with current SIMD multiplication.
  A private 96-byte stack buffer holds the packed low/high/cross operands.
  Setup stages the first group; the last iteration stages itself again to
  keep every load within the supplied input. It uses the PMULL13 reducer.
  This tests scalar store pairing, including the cost of loading the staged
  vectors and the initial preparation.

| Work per 64 input bytes | PMULL12/13 | K32 | K32-load/sched | K32-tbl | GPR reduction | Staged input |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Little-endian loop instructions, including control | 370/375 | 267 | 375 | 367 | 379/384 | 394/399 |
| Byte PMULL/PMULL2 instructions | 99 | 75 | 75 | 75 | 96 | 99 |
| Key context bytes | 736 | 688 | 688 | 688 | 736 | 736 |

Alternating counts are the forward/reverse halves of the existing 128-byte
assembly loop. The K32 loop processes 64 bytes per iteration and walks its
432-byte table forward. Its scalar-load variants also preload 48 bytes once
per call. Lower static instruction count alone does not predict performance.

### Native Cortex-A53 results

CPU3, 1.2 GHz, performance governor, aligned input and seed 1708171853.
Each cell pools ten trials: five 300 ms trials with a 100 ms warmup, followed
by another five with both algorithm and size order reversed. All 560 timing
rows pass preflight/final-digest and guard verification. Rates below are
payload Gbit/s from the shared userspace assembly, not full GCM or kTLS.

| Variant (`ghash-neon-4x-…`) | 64 B | 128 B | 256 B | 1440 B | 16384 B | 16400 B | 65536 B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| pmull13 | 1.594247 | 1.702861 | 1.767708 | 1.802210 | 1.835504 | 1.833267 | 1.826940 |
| pmull12 | 1.570145 | 1.693433 | 1.765073 | 1.806033 | 1.841801 | 1.838880 | 1.833903 |
| k32 | 1.668195 | 1.727079 | 1.759235 | 1.763195 | 1.788505 | 1.786004 | 1.778298 |
| k32-load | 1.689807 | 1.769212 | 1.811277 | 1.823004 | 1.854535 | 1.851652 | 1.843615 |
| k32-sched | 1.748192 | 1.832241 | 1.877540 | 1.889582 | 1.923692 | 1.920631 | 1.913527 |
| k32-tbl | 1.774066 | 1.869282 | 1.920981 | 1.935279 | 1.972928 | 1.969209 | 1.962749 |
| gpr-reduce | 1.579624 | 1.699523 | 1.769833 | 1.807948 | 1.842174 | 1.840199 | 1.833022 |
| stage | 1.507209 | 1.654464 | 1.745020 | 1.804711 | 1.844306 | 1.842077 | 1.837615 |

`4x-k32-tbl` is fastest at all seven tested lengths: every trial exceeds
every other variant's trial at the same length. At 64 KiB it improves on
PMULL12 by **7.03%** and PMULL13 by **7.43%**. The unmodified K32 schedule
is 3.03% slower than PMULL12 despite executing far fewer instructions.
Scalar key loads recover that loss; reconstruction scheduling and simpler
permutations provide the larger gain. Scalar reduction is effectively tied
with PMULL12. Staging input gains only 0.20% at 64 KiB and loses 4.01% at
64 bytes, where its bootstrap cost is not amortized.

Sixteen additional 64-KiB perf runs collect a single non-multiplexed group:
`cycles:u,instructions:u,re6:u,re5:u,re7:u`. Each algorithm runs in both
orders for three seconds after a 100 ms warmup. Ratios pool the four complete
one-second intervals ending near two and three seconds, excluding startup
and the final partial interval. All selected counters report 100% scheduling.

| Variant | Retired IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| pmull13 | 1.1120 | 1.0456% | 0.0013% | 0.5109% |
| pmull12 | 1.1149 | 0.4498% | 0.0021% | 0.5420% |
| k32 | 0.7758 | 7.2636% | 0.2912% | 0.5188% |
| k32-load | 1.1302 | 8.4387% | 0.0010% | 0.4710% |
| k32-sched | 1.1725 | 5.0028% | 0.0010% | 0.4976% |
| k32-tbl | 1.1769 | 5.1305% | 0.0016% | 0.5170% |
| gpr-reduce | 1.1424 | 0.4500% | 0.0013% | 0.5013% |
| stage | 1.1904 | 1.2018% | 0.0019% | 0.3962% |

These A53 interlock events are neither a complete nor necessarily disjoint
breakdown of stall cycles. `k32-tbl` still has more SIMD interlocks than the
controls, but its complete loop runs faster. The results support this
implementation on the tested A53; A55 performance remains unmeasured.

### Validation and reproduction

The userspace executable and external ARM64 module build with GCC 14.2 and
warnings as errors (`W=1 KCFLAGS=-Werror` for the module). Strict checkpatch
has no code-style findings. The 17 existing functions in the modified core
assembly retain their instruction encodings; the other existing assembly
files are unchanged. The final executable's `.text` matches the binary used
for these native measurements.

The six new variants pass 60,480 full selftest comparisons on native CPU3,
and another 60,480 in user-mode Cortex-A53 QEMU TCG. Coverage includes four
key/data patterns, offsets 0/1/7/15, continuing state, known answers, ABI
preservation, guard pages and lengths through 131,201 bytes. Independent
bare assembly fixtures pass 1,152 cases per endianness, including nonzero
initial state, three keys, four offsets and 16 block counts through 4,097.

Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests passes
54,240 direct-shash comparisons and 342 benchmark rows for the six new
drivers. These cover partial tails, streaming, export/import, source offsets,
and 17-byte/MSS-sized SG fragments. Module unloading succeeds, with no
kernel warnings or KASAN findings. Native measurements load no kernel module.

The [userspace instructions](../../utils/ghash-bench-userspace/README.md#extra-karatsuba-and-scalar-experiments-release-3)
give the comparison commands. Source and result captures are kept under
`/tmp/ghash-test` on the device. No OpenWrt package or firmware image was built.

## Deeper Karatsuba splits (release 24)

Four additional drivers extend the experiment down to 16-bit and 8-bit
operands, with vector key loads or overlapped scalar key loads for each.
They retain the four-block GHASH formula, outer 128/64/32-bit Karatsuba
layers, byte-PMULL reducer and two-block short/tail path.

`4x-k16` replaces each 32-bit product with three 16-bit products: low halves,
high halves and XORed halves. Each 16-bit product uses direct byte products.
`4x-k8` applies Karatsuba once more, replacing those direct products with
three byte products and another XOR-based reconstruction. Both accumulate
raw products across four GHASH terms before reconstruction, then interleave
independent reconstruction chains. Fixed `TBL` permutations position the
middle terms and zero-fill unused bytes.

Each 688-byte context starts with the existing 256-byte fallback context.
The remaining 432 bytes hold original, swapped and XORed key operands in
the layout consumed by that variant. Setup computes H through H^4 once.
The `-load` variants prefetch the next 48-byte table group through scalar
registers while SIMD consumes the current group, wrapping within the key
context. No source overread, stack staging or data-dependent lookup is used.

| Work per 64 input bytes | K32-tbl control | K16 | K16-load | K8 | K8-load |
| --- | ---: | ---: | ---: | ---: | ---: |
| Little-endian loop instructions, including control | 367 | 282 | 390 | 354 | 462 |
| Byte PMULL/PMULL2 instructions, including reduction | 75 | 57 | 57 | 48 | 48 |
| Key context bytes | 688 | 688 | 688 | 688 | 688 |

All five loops process 64 input bytes per assembly iteration. The extra
Karatsuba layers reduce multiply instructions but add operand formation,
XOR corrections, lane packing and dependent reconstruction steps. Scalar
loads add 108 instructions per group relative to vector loads, allowing
some work to issue alongside SIMD on the A53. These variants measure the
complete tradeoff; fewer PMULL instructions alone do not imply faster GHASH.

Measured on the Cortex-A53 at 1.2 GHz, CPU3, performance governor. Each
cell is the pooled median of ten 300-ms trials: five in forward algorithm
and length order, five in reverse order, with 100-ms warmups. Allocation,
key setup and verification are untimed. Rates are Gbit/s of GHASH input.

| Bytes | K32-tbl | K16 | K16-load | K8 | K8-load |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 1.769063 | 1.615068 | 1.636665 | 1.234374 | 1.246436 |
| 128 | 1.867884 | 1.670591 | 1.710645 | 1.265729 | 1.288509 |
| 256 | 1.919446 | 1.699101 | 1.749143 | 1.282647 | 1.311014 |
| 1440 | 1.936187 | 1.706073 | 1.762271 | 1.294193 | 1.325922 |
| 16384 | 1.972567 | 1.727920 | 1.788641 | 1.298816 | 1.332730 |
| 16400 | 1.970321 | 1.725547 | 1.786777 | 1.298088 | 1.332363 |
| 65536 | 1.964473 | 1.718960 | 1.777987 | 1.294064 | 1.328383 |

`k32-tbl` remains faster than all four deeper-split implementations at every
tested length. At 64 KiB, the best K16 variant is 9.49% slower and the best
K8 variant is 32.38% slower. Overlapped scalar key loads improve K16 by
3.43% and K8 by 2.65%, but do not recover the gap.

Separate 64-KiB perf runs use the non-multiplexed user-mode event group
`{cycles:u,instructions:u,re6:u,re5:u,re7:u}`. The table pools four complete
one-second intervals per variant, two from each run order, after excluding
startup and partial final intervals. Every selected event ran 100% of its
interval.

| Variant | IPC | SIMD interlock / cycles | AGU interlock / cycles | Load interlock / cycles |
| --- | ---: | ---: | ---: | ---: |
| `4x-k32-tbl` | 1.177 | 5.13% | 0.002% | 0.49% |
| `4x-k16` | 0.792 | 6.18% | 0.282% | 0.45% |
| `4x-k16-load` | 1.133 | 7.26% | 0.001% | 0.53% |
| `4x-k8` | 0.749 | 14.17% | 0.212% | 0.33% |
| `4x-k8-load` | 1.002 | 15.18% | 0.001% | 0.37% |

The vector-load K16 loop executes fewer instructions than K32-tbl but has
much lower IPC. Deeper splitting trades multiplies for operand shuffles
and XOR reconstruction, with more SIMD interlocks in the byte-split
variants. Scalar loads improve instruction overlap but still leave more
cycles per input byte. These counters are overlapping stall indicators,
not a complete cycle breakdown. The results evaluate these schedules on
A53; they do not establish a limit for every deeper Karatsuba schedule or
for A55.

Validation: GCC 14.2 userspace and external ARM64 module builds pass with
warnings treated as errors. The four new variants pass 40,320 full
userspace selftest checks on both the device and Cortex-A53 QEMU TCG,
including offsets, guarded boundaries, continuing state and ABI checks
through 131,201 bytes. All 34 selectable variants pass the short selection
check. Independent assembly fixtures pass 768 cases per endianness with
nonzero initial state and up to 4,097 blocks. Kernel Cortex-A53 QEMU TCG
with KASAN and full crypto selftests passes 36,160 direct-shash comparisons
and 228 benchmark rows, including fragmented updates, partial tails and
export/import. Native measurement accounting and hashes were checked for
350 timing rows and ten PMU runs. Existing assembly files are unchanged.
No OpenWrt package or firmware image was built, and the new module was not
loaded on the device. The four new drivers remain explicit comparison
choices below stock priorities; keep `ghash-neon-4x-k32-tbl` for the best
measured bulk performance.

## Single-block K32 (release 25)

`ghash-neon-k32` applies the 128/64/32-bit Karatsuba hierarchy to one GHASH
block at a time. It uses the ordinary 16-byte reflected-key context, forms
the rotated key operands once on entry, and keeps them in registers for
the whole call. Each iteration reads exactly 16 input bytes, XORs the
previous digest, multiplies, reduces and advances the digest. There is no
aggregation, power-of-H table, per-iteration key load or input overread.

Writing the input as four 32-bit words a0..a3, the nine products are the
four diagonal products ai*bi, followed by products of the XOR pairs
(a0+a1), (a2+a3), (a0+a2), (a1+a3), and finally (a0+a1+a2+a3), each
multiplied by the corresponding key expression. Here addition means XOR.
Eight products are packed into two groups of four, using both PMULL and
PMULL2; the final product occupies only half of each byte-PMULL operand.
Fixed TBL masks reconstruct the 32-bit products, then independent chains
reconstruct the three 64-bit products before the outer Karatsuba fold and
byte-PMULL reduction.

`ghash-neon-k32-packed` fills the otherwise unused lanes for the ninth
product. One PMULL holds the unrotated and distance-two byte products;
another holds both directions of the distance-one products. This saves
two PMULL instructions, at the cost of extra input packing and output
folding. Both variants use only baseline ARM64 ASIMD and share the normal
single-block key setup and streaming wrappers.

| Per 16 input bytes | PMULL13 control | PMULL12 control | Schoolbook | K32 | K32-packed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Little-endian loop instructions, including control | 133 | 132 | 123 | 114 | 115 |
| Byte PMULL/PMULL2 instructions, including reduction | 27 | 27 | 35 | 23 | 21 |
| Key context bytes | 16 | 16 | 16 | 16 | 16 |

These counts are per single-block iteration for all five implementations.
The four-block K32 implementations remain separate controls.

On the Cortex-A53 at 1.2 GHz, CPU3, performance governor, ten-trial pooled
medians in Gbit/s are:

| Bytes | PMULL13 | PMULL12 | Schoolbook | K32 | K32-packed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.854574 | 0.845042 | 0.879708 | 0.889428 | 0.864285 |
| 32 | 0.971260 | 0.971612 | 1.006585 | 1.029398 | 1.003025 |
| 64 | 1.042995 | 1.050331 | 1.085651 | 1.118899 | 1.091421 |
| 128 | 1.082385 | 1.093652 | 1.129262 | 1.168752 | 1.141324 |
| 256 | 1.097541 | 1.111386 | 1.145680 | 1.187563 | 1.159815 |
| 1440 | 1.120143 | 1.136808 | 1.171931 | 1.217855 | 1.189419 |
| 16384 | 1.124381 | 1.140989 | 1.176887 | 1.223402 | 1.194626 |
| 16400 | 1.125036 | 1.140922 | 1.177006 | 1.223339 | 1.195465 |
| 65536 | 1.123105 | 1.140144 | 1.175232 | 1.222356 | 1.193237 |

Each cell combines five 300-ms runs in forward algorithm/length order and
five in reverse order, with 100-ms warmups. Key setup, allocation and
verification are outside timing; the assembly entry setup and userspace
ABI wrapper are included. K32 is faster than the other four implementations
in every trial at every tested length. At 64 KiB, it improves on PMULL13
by 8.84%, PMULL12 by 7.21% and schoolbook by 4.01%. The packed variant is
2.38% slower than K32 despite its lower multiply count. These are userspace
GHASH rates; no GCM, Crypto API or kTLS overhead is included.

Separate 64-KiB perf runs use the non-multiplexed event group
`{cycles:u,instructions:u,re6:u,re5:u,re7:u}`. Four complete one-second
intervals per variant, two per run order, exclude startup and the final
partial interval; all selected counters ran 100% of each interval.

| Variant | IPC | SIMD interlock / cycles | AGU interlock / cycles | Load interlock / cycles |
| --- | ---: | ---: | ---: | ---: |
| `orig-pmull13` | 0.976 | 4.40% | 0.0001% | 0.16% |
| `orig-pmull12` | 0.984 | 2.98% | 0.0002% | 0.11% |
| `schoolbook` | 0.944 | 6.14% | 0.0006% | 0.18% |
| `k32` | 0.910 | 8.78% | 0.0008% | 0.17% |
| `k32-packed` | 0.897 | 10.13% | 0.0008% | 0.20% |

K32 reduces total instructions enough to win even though its IPC is lower
and its SIMD-interlock count is higher than the PMULL controls. The packed
variant adds a dependent shuffle/fold path and has still more SIMD
interlocks. The counters are overlapping stall indicators, not a complete
cycle breakdown. CPU time at the fixed 1.2-GHz clock corresponds to about
125.47 cycles per 16-byte block for K32 with 64-KiB calls, versus 136.55 for
PMULL13. Those are whole-benchmark CPU-time estimates, not isolated loop
latencies. The comparison supports K32 among these single-block choices on
A53; it does not replace the separate four-block throughput comparison.

Validation: GCC 14.2 userspace and external ARM64 module builds pass with
warnings treated as errors. Each of the two new implementations passes
10,080 full userspace selftest comparisons on both the device and
Cortex-A53 QEMU TCG, through 131,201 bytes, covering four key/data patterns,
nonzero continuing state, offsets, guard pages and ABI preservation. All
36 selectable variants pass the short selection check. Independent
assembly fixtures pass 384 cases per endianness, with nonzero initial
state and up to 4,097 blocks. Kernel Cortex-A53 QEMU TCG with KASAN and full
crypto selftests passes 18,080 direct-shash comparisons and 114 benchmark
rows, including fragmented input, partial tails and state export/import.
Accounting and hashes were checked for 450 native timing rows and ten PMU
runs. Existing assembly files remain unchanged. No OpenWrt package or
firmware image was built, and no new kernel module was loaded on the
device. All native files remain under `/tmp/ghash-test`.

## Single-block mask-load addressing (release 26)

The single-block K32 entry code loads its first four permutation masks with
`ld1 {v24.16b-v27.16b}, [x4]` and the fifth with
`ldr q28, [x4, #64]`. Both use the unchanged base address, removing the
load-to-load address dependency from post-indexed writeback. No extra
address register or address calculation is needed. This runs once per
call; the little-endian hashing loops retain their original instruction
encodings and offsets.

`LDR Q` treats the load as a 128-bit element, while `LD1 .16b` loads sixteen
bytes. Big-endian builds reverse the fifth mask's bytes after loading it,
preserving the existing TBL indices. Little-endian builds need no conversion.

Validation: warning-clean GCC 14.2 userspace and external ARM64 module
builds; 20,160 userspace selftest checks each on the A53 and QEMU TCG;
384 independent assembly comparisons per endianness. The change was
also measured before and after on CPU3 at 1.2 GHz with both K32 variants,
16/64/1440/65536-byte inputs and ten trials per comparison cell. Kernel
streaming/KASAN coverage above is from release 25; it was not repeated for
this entry-only change. No OpenWrt package or firmware image was built.

On the same A53 CPU3 at 1.2 GHz, pooled 16-byte-call medians change from
0.889400 to 0.900192 Gbit/s for K32 (+1.21%) and from 0.864982 to 0.874340
for K32-packed (+1.08%). The 64-KiB medians change by +0.04% and -0.01%,
respectively, showing no meaningful bulk-throughput change. Each median
combines five 200-ms trials in each execution order, with 50-ms warmups.

## Direct SIMD key loads for K32-TBL (release 27)

Three additional variants keep the multiplication and table-based
reconstruction of `ghash-neon-4x-k32-tbl`, but load keys directly into SIMD
registers. They isolate loading strategy from the reconstruction changes
that distinguished earlier K32 variants:

- `ghash-neon-4x-k32-tbl-simd` loads each group immediately before use with
  `ld1 {v9.2d-v11.2d}, [x4], #48`.
- `ghash-neon-4x-k32-tbl-simd-pipe` moves the next group's identical `LD1`
  after the current group's last multiply and before its final XORs.
- `ghash-neon-4x-k32-tbl-simd-ldr` loads the next vectors individually:
  `q10` after its final multiply, then `q9`, then `q11`. Each uses fixed
  offsets from `x4`; a single addition advances the pointer at group end.
  Big-endian builds exchange each loaded vector's 64-bit lanes to match
  the existing `LD1 .2d` key layout.

The scalar baseline overlaps six general-purpose-register loads with
SIMD multiplication, then uses `FMOV`/`INS` to transfer the next keys.
All variants transfer the same 432 bytes of keys per 64-byte loop,
use the same 688-byte context and retain the same fallback for fewer
than four blocks. Message input still uses SIMD loads in every version.
The pipelined versions preload one group at entry and wrap to the first
valid key group at the end of each loop, including the last iteration;
there is no key-table overread.

The added drivers have priorities 63, 62 and 61. The existing scalar-load
function retains identical instruction encodings. No key preparation,
reconstruction, reduction or input-boundary logic changes are required.

### Cortex-A53 comparison

Native userspace results on CPU3 at 1.2 GHz with the performance governor,
GCC 14.2, `-O2 -g -Werror -static`. Each cell is the pooled median of ten
300 ms trials, five in each algorithm/size order, with 100 ms warmup per
invocation. All four variants were measured together in the final binary.
Payload throughput is Gbit/s:

| Bytes | Scalar loads | Immediate LD1 | Earlier LD1 | Scheduled LDR Q |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 1.769306 | 1.743848 | 1.700432 | 1.695580 |
| 128 | 1.866285 | 1.818677 | 1.789299 | 1.788141 |
| 256 | 1.918949 | 1.859835 | 1.838522 | 1.838052 |
| 1440 | 1.935461 | 1.868002 | 1.855104 | 1.854813 |
| 16384 | 1.973939 | 1.898790 | 1.888111 | 1.887480 |
| 16400 | 1.970603 | 1.897335 | 1.885024 | 1.885500 |
| 65536 | 1.963172 | 1.889837 | 1.880269 | 1.881126 |

At each size, every scalar-load trial is faster than every SIMD-load trial.
At 64 KiB, the immediate SIMD version is 3.74% slower than scalar loads;
the earlier `LD1` and individually scheduled `LDR Q` versions are 4.22%
and 4.18% slower, respectively. Keep the scalar-load `4x-k32-tbl` on this
A53. These schedules do not recover the scalar version's throughput by
moving SIMD loads earlier.

Eight separate 3-second perf runs measured the 64-KiB case, one per variant
in each order. The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` ran
without multiplexing. Pooling the two complete middle 1-second intervals
from each order gives:

| Variant | Instructions / 64-byte loop | Byte PMULLs / loop | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar loads | 367 | 75 | 1.1765 | 5.1288% | 0.0016% | 0.5487% |
| Immediate LD1 | 259 | 75 | 0.7992 | 4.0112% | 0.3098% | 0.5166% |
| Earlier LD1 | 259 | 75 | 0.7956 | 4.9147% | 0.0016% | 0.4215% |
| Scheduled LDR Q | 286 | 75 | 0.8786 | 4.9148% | 0.0016% | 0.4138% |

The two grouped-load variants have identical loop instruction counts.
Earlier loading removes most AGU interlocks, but measured SIMD interlocks
increase and overall throughput does not improve. The individually
scheduled loads have essentially the same bulk throughput as the earlier
grouped load despite their different architectural instruction count.
This is consistent with SIMD load issue/resource costs limiting the
benefit of hiding load-to-use latency; it does not uniquely identify a
microarchitectural cause. A multi-register `LD1` counts as one instruction
but still performs the memory transfers.

Instruction counts include loop control and reduction. IPC covers the
measured userspace instruction stream, dominated by the hashing loop.
The interlock events are not a complete or disjoint breakdown of cycles.
These results compare the four schedules on A53; they do not establish
the best schedule on A55 or full GCM/kTLS throughput.

### Validation and reproduction

- Userspace and external ARM64 module builds pass with warnings as errors;
  strict code-style checks pass.
- The final four-way comparison passes 40,320 native and 40,320 user-mode
  Cortex-A53 QEMU TCG selftest comparisons through 131,201 bytes, including
  four key/payload patterns, offsets, continuing state, guard pages and ABI.
- Independent assembly checks cover 768 cases per endianness across the
  four variants, including nonzero chaining state, three keys and up to
  4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with full crypto selftests and KASAN passes
  36,160 direct-shash checks and 228 benchmark rows across the four variants.
  Coverage includes update/final, export/import, partial tails and SG chunks
  of 17 and 1440 bytes. Each added schedule was checked as it was introduced.
- All 280 final native timing rows and eight PMU runs pass verification;
  captured hashes, row accounting and counter scheduling were checked.

The final source, executable and captures are under `/tmp/ghash-test` on
the device, using the `vector-` prefix; the executable is `vector-simd`.
The earlier two-way and three-way captures use `tbl-` and `sched-` prefixes.
No kernel module was loaded on the device. The [userspace commands](../../utils/ghash-bench-userspace/README.md#direct-simd-key-loads-release-7)
select all four names explicitly. No OpenWrt package or firmware image was built.

## Eight-block K32 aggregation (release 28)

`ghash-neon-8x-k32-tbl` processes two four-block groups while retaining
three unreduced Karatsuba product sums in `v6-v8`. The first group uses
`H^8..H^5` and folds the incoming digest into its first block. The second
uses `H^4..H`, XORs its corresponding product sums into the same registers,
and performs the outer merge and field reduction once for all 128 bytes.
Each group's internal 32-bit-product reconstruction still runs separately.

Both groups retain the scalar key-load schedule of `ghash-neon-4x-k32-tbl`.
The SIMD register footprint is unchanged, each input block is read once,
and the hot loop has no scratch stores or spills. The precomputed context
keeps the existing 688-byte K32 context as a prefix, followed by 432 bytes
for `H^8..H^5`, for a total of 1120 bytes. Calls and tails below eight blocks
branch to the existing four-block implementation. The pipelined key loads
switch from the upper table back to the lower table and then wrap to the
upper table, always inside the allocated context.

The comparison module adds a separately selectable driver at priority 60;
existing driver priorities remain unchanged. Input preparation and final
reduction are shared assembly macros, and the seven pre-existing functions
in `ghash-neon-k32.S` retain identical instruction encodings. Kernel and
userspace key setup share their respective existing K32 table builders.

| Work per 128 input bytes | Two 4-block iterations | One 8-block iteration |
| --- | ---: | ---: |
| Loop instructions, including control | 734 | 716 |
| Byte PMULL/PMULL2 instructions | 150 | 147 |
| Field reductions | 2 | 1 |
| Key bytes transferred in the loop | 864 | 864 |

The new version saves 18 instructions (2.45%) per 128 bytes. It increases
key setup work and context size; allocation and key setup are outside the
throughput measurements below.

### Cortex-A53 comparison

Native userspace measurements used CPU3 at 1.2 GHz with the performance
governor and GCC 14.2 (`-O2 -g -Werror -static`). Each cell is the pooled
median of ten 300 ms trials, five in each algorithm/size order, with
100 ms warmup per invocation. Throughput is Gbit/s:

| Bytes | 4-block K32-TBL | 8-block K32-TBL | Change |
| ---: | ---: | ---: | ---: |
| 64 | 1.774481 | 1.773070 | -0.08% |
| 112 | 1.378039 | 1.373576 | -0.32% |
| 128 | 1.868746 | 1.932547 | +3.41% |
| 144 | 1.646027 | 1.686276 | +2.45% |
| 256 | 1.919841 | 1.985505 | +3.42% |
| 1440 | 1.935068 | 1.996839 | +3.19% |
| 16384 | 1.972844 | 2.035803 | +3.19% |
| 16400 | 1.968742 | 2.034792 | +3.35% |
| 65536 | 1.962846 | 2.026650 | +3.25% |

Every 8-block trial beats every 4-block trial at each tested size of 128
bytes or more. The short-call fallback is within 0.4% of the existing
implementation at 64 and 112 bytes. This makes the 8-block variant the
preferred measured K32-TBL schedule for larger inputs on this A53.

Four separate 3-second perf runs measured the 64-KiB case, one per variant
in each order. The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` ran
without multiplexing. Pooling the two complete middle 1-second intervals
from each order gives:

| Variant | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| 4-block K32-TBL | 1.1769 | 5.1307% | 0.0016% | 0.5116% |
| 8-block K32-TBL | 1.1861 | 4.6381% | 0.0014% | 0.5152% |

The gain combines fewer instructions per byte with slightly higher IPC.
The raw interlock events are not a complete or disjoint cycle breakdown.
These are userspace GHASH measurements on A53, not complete GCM/kTLS or
A55 results.

### Validation and reproduction

- Userspace and external ARM64 module builds pass with warnings as errors;
  strict code-style checks pass.
- Both variants pass 20,160 native and 20,160 user-mode Cortex-A53 QEMU TCG
  selftest comparisons through 131,201 bytes, with four key/payload patterns,
  offsets, continuing state, guard pages and ABI checks.
- Independent assembly fixtures pass 384 cases per endianness, including
  nonzero initial state, three keys and lengths through 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with full crypto selftests and KASAN passes
  18,080 direct-shash comparisons and 114 benchmark rows. Coverage includes
  update/final, export/import, partial tails and SG chunks of 17 and 1440 bytes.
- All 180 native timing rows and four PMU runs pass verification; captured
  hashes, row accounting and counter scheduling were checked.

Native sources and captures remain under `/tmp/ghash-test` with the
`eight-` prefix; the executable is `eight-final`. No kernel module was
loaded on the device, and no OpenWrt package or firmware image was built.
The [userspace commands](../../utils/ghash-bench-userspace/README.md#eight-block-k32-aggregation-release-8)
select the two implementations explicitly.

## Scratch key stores (release 29)

Four comparison drivers route K32's scalar key loads through a 48-byte,
16-byte-aligned stack buffer. Three `STP X,X` instructions store each next
set of keys while the current SIMD products are computed. This replaces
the six `FMOV`/`INS` transfers of the corresponding K32-TBL control:

- `ghash-neon-4x-k32-stp` reloads all three keys with one
  `LD1 {v9.2d-v11.2d}, [sp]` after the product XORs.
- `ghash-neon-4x-k32-stp-pipe` schedules three `LDR Q` reloads individually,
  after each old key's last use and with arithmetic separating stores from
  reloads. Big-endian builds exchange the two 64-bit lanes after each load.
- `ghash-neon-8x-k32-stp` and `ghash-neon-8x-k32-stp-pipe` apply the same
  two schedules to eight-block aggregation.

The scalar loads retain their original fixed key offsets. Scratch slots
are fully written before each reload and wiped before releasing the stack
frame. The key contexts remain 688 and 1120 bytes; short calls and tails
use the existing fallbacks, with the stack restored before branching to
one. The eight pre-existing functions in `ghash-neon-k32.S` retain identical
instruction encodings. Comparison priorities are 59 through 56.

| Variant | Bytes per loop | Instructions | Byte PMULL/PMULL2 | Scratch stores | Extra scratch bytes written + read |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4x K32-TBL | 64 | 367 | 75 | 0 | 0 |
| 4x STP + grouped reload | 64 | 349 | 75 | 27 | 864 |
| 4x STP + scheduled reloads | 64 | 367 | 75 | 27 | 864 |
| 8x K32-TBL | 128 | 716 | 147 | 0 | 0 |
| 8x STP + grouped reload | 128 | 680 | 147 | 54 | 1728 |
| 8x STP + scheduled reloads | 128 | 716 | 147 | 54 | 1728 |

Counts include loop control. Setup and the three clearing stores at return
are outside the hot loop but included in timing. `LD1` counts as one retired
instruction while transferring 48 bytes; the grouped variant's lower
instruction count does not establish a lower execution cost. Scratch
traffic repeatedly accesses the small stack area and is not a measurement
of DRAM traffic.

### Cortex-A53 results

All six implementations ran on CPU3 at 1.2 GHz with the performance governor,
GCC 14.2 (`-O2 -g -Werror -static`), aligned input and seed 1708171853.
Each cell is the median of ten 300 ms trials: five in each algorithm/size
order, with 100 ms warmup per invocation. Allocation, key setup and
verification are untimed. Rates are payload Gbit/s.

| Bytes | 4x K32-TBL | 4x STP grouped | 4x STP scheduled | 8x K32-TBL | 8x STP grouped | 8x STP scheduled |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 1.773806 | 1.614294 | 1.667996 | 1.773677 | 1.768815 | 1.768387 |
| 112 | 1.378446 | 1.318557 | 1.338288 | 1.373300 | 1.372874 | 1.373016 |
| 128 | 1.869743 | 1.732277 | 1.761712 | 1.934354 | 1.784990 | 1.816208 |
| 144 | 1.645987 | 1.535984 | 1.567393 | 1.685290 | 1.581171 | 1.602574 |
| 256 | 1.920835 | 1.780880 | 1.811516 | 1.986023 | 1.820118 | 1.869289 |
| 1440 | 1.935979 | 1.770808 | 1.832789 | 1.997435 | 1.838546 | 1.888035 |
| 16384 | 1.970661 | 1.815503 | 1.863518 | 2.038701 | 1.853006 | 1.923707 |
| 16400 | 1.971030 | 1.814269 | 1.862328 | 2.035231 | 1.849998 | 1.920780 |
| 65536 | 1.962524 | 1.788650 | 1.853920 | 2.026793 | 1.841987 | 1.910749 |

At 64 KiB, grouped scratch reloads are 8.86% slower than the four-block
control and 9.12% slower than the eight-block control. Scheduling reloads
reduces those losses to 5.53% and 5.73%, respectively. At every tested size
where the scratch loop runs, every scratch trial is slower than every
corresponding control trial. Eight-block calls below 128 bytes use the
existing fallback and do not exercise the scratch loop.

Twelve separate 3-second perf runs cover each algorithm in both orders.
The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs without
multiplexing. Pooling the two complete middle 1-second intervals from
each order gives:

| Variant | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| 4x K32-TBL | 1.1771 | 5.1316% | 0.0016% | 0.4967% |
| 4x STP grouped | 1.0293 | 4.7185% | 0.0012% | 2.2440% |
| 4x STP scheduled | 1.1123 | 4.8487% | 0.0016% | 0.4688% |
| 8x K32-TBL | 1.1858 | 4.6367% | 0.0014% | 0.5382% |
| 8x STP grouped | 1.0325 | 4.2511% | 0.0013% | 2.3764% |
| 8x STP scheduled | 1.1180 | 4.3715% | 0.0013% | 0.5707% |

The grouped reloads add visible load interlocks; scheduling brings that
counter close to the controls, but still loses throughput and IPC. The
scalar stores can pair with SIMD arithmetic in isolation, yet routing keys
back through memory adds load/store work to the complete loop. These
counters do not isolate store-forwarding latency from issue-resource costs,
and their categories are not a complete or disjoint cycle breakdown.
Keep `ghash-neon-8x-k32-tbl` as the preferred measured bulk implementation
on this A53. This result applies to these scratch-key schedules; it does
not establish that every possible use of scratch storage is slower.

### Validation and reproduction

- Userspace and external ARM64 module builds pass with warnings as errors;
  strict code-style checks pass.
- All six variants pass 60,480 native and 60,480 user-mode Cortex-A53
  QEMU TCG comparisons through 131,201 bytes, including input offsets,
  continuing state, guard pages and ABI preservation.
- Independent fixtures pass 1,152 cases per endianness, with nonzero
  initial state, three keys, four offsets and lengths through 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with full crypto selftests and KASAN passes
  36,160 direct-shash comparisons and 228 benchmark rows across the four
  scratch variants, covering update/final, export/import, partial tails
  and SG chunks of 17 and 1440 bytes.
- All 540 native timing rows and 12 PMU runs pass verification. Capture
  hashes, row accounting and counter scheduling were checked.

Native sources, executable and captures use the `stp-` prefix under
`/tmp/ghash-test`; the executable is `stp-final`. No kernel module was
loaded on the device and no OpenWrt package or firmware image was built.
The [userspace commands](../../utils/ghash-bench-userspace/README.md#scratch-key-stores-release-9)
select all six comparison targets.

## Shared K32 reconstruction and lane scheduling (release 30)

The previous eight-block loop reconstructs complete 64-bit products for
H8..H5, reconstructs another set for H4..H1, then XORs the results. The new
`ghash-neon-8x-k32-merge` instead XORs the byte-product diagonals from both
groups before reconstructing each of the three outer Karatsuba products.
Reconstruction is linear over XOR, so it needs three executions per eight
blocks instead of six. The outer Karatsuba merge and field reduction still
run once per eight blocks.

All eight input blocks stay in SIMD registers. After folding the incoming
digest into the first block, v31 holds one input pair; v29 temporarily holds
another. The reduction constant is recreated after those inputs are consumed.
The existing 1120-byte context is reused, with traversal alternating between
corresponding components of its H8..H5 and H4..H1 tables. Each input is read
once. No new stack storage, register spills or extra key bytes are needed.
Calls below 128 bytes and the remaining tail use the existing four-block
fallback. All 12 pre-existing K32 functions retain identical instruction
encodings.

Seven new driver names provide the combinations, with priorities 55 through
49. Name suffixes below follow `ghash-neon-8x-k32-`:

| Suffix | Reconstruction | Key-load / transfer schedule | Instructions per 128 bytes |
| --- | --- | --- | ---: |
| `tbl` (control) | Per four blocks | Overlapped scalar loads; grouped lane transfers | 716 |
| `merge` | Shared across eight blocks | Same scalar schedule | 673 |
| `merge-simd` | Shared | Grouped `LD1` immediately before each product group | 457 |
| `merge-ldr` | Shared | Each `LDR Q` follows that old key vector's last use | 511 |
| `merge-early` | Shared | Move each adjacent `FMOV`/`INS` pair to its key's last use | 673 |
| `early` | Per four blocks | Same early adjacent pairs | 716 |
| `merge-lanes` | Shared | Separate each `FMOV` and `INS` with independent arithmetic | 673 |
| `lanes` | Per four blocks | Same separated lane writes | 716 |

All eight loops use 147 byte PMULL/PMULL2 instructions per 128 bytes.
Instruction counts are for little-endian hot loops, including loop control
but excluding call setup and tails. Direct multi-register `LD1` counts as
one instruction while transferring 48 bytes; its lower count alone does not
predict execution time. All variants require only baseline ASIMD.

### Cortex-A53 results

Measurements use CPU3 at 1.2 GHz with the performance governor, GCC 14.2
(`-O2 -g -Werror -static`), aligned input and seed 1708171853. Each cell is
the median of ten 300 ms trials, five in each algorithm/size order, with
100 ms warmup per invocation. Key setup, input preparation and verification
are untimed. Rates are payload Gbit/s from the userspace block entry points.

| Bytes | tbl | merge | merge-simd | merge-ldr | merge-early | early | merge-lanes | lanes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 1.772891 | 1.767791 | 1.772309 | 1.772982 | 1.767922 | 1.773839 | 1.768123 | 1.773583 |
| 112 | 1.374608 | 1.373914 | 1.374616 | 1.374040 | 1.373975 | 1.373906 | 1.373844 | 1.374064 |
| 128 | 1.933301 | 2.082096 | 2.050295 | 2.016128 | 1.961828 | 1.829673 | 2.019886 | 1.880135 |
| 144 | 1.685578 | 1.788291 | 1.763008 | 1.741274 | 1.709005 | 1.614647 | 1.746914 | 1.649121 |
| 256 | 1.986036 | 2.144770 | 2.094654 | 2.071843 | 2.017436 | 1.876611 | 2.078450 | 1.929313 |
| 1440 | 1.996725 | 2.153471 | 2.092798 | 2.080335 | 2.029555 | 1.888605 | 2.089924 | 1.941850 |
| 16384 | 2.037510 | 2.206803 | 2.138686 | 2.127059 | 2.073109 | 1.922629 | 2.139268 | 1.979570 |
| 16400 | 2.035611 | 2.203009 | 2.134524 | 2.122937 | 2.069471 | 1.920400 | 2.134820 | 1.976131 |
| 65536 | 2.026865 | 2.192719 | 2.124001 | 2.116340 | 2.061777 | 1.915358 | 2.124169 | 1.968599 |

At 64 KiB, shared reconstruction with the original scalar schedule reaches
2.192719 Gbit/s versus 2.026865 for the previous eight-block loop,
a 8.18% gain. The gain is 8.31% at 16 KiB and 7.85% at 1440 bytes.
At each tested size of 128 bytes or more, every `merge` trial beats every
trial of the other seven variants. Inputs below 128 bytes use the common
fallback and do not exercise the new loop.

Moving adjacent `FMOV`/`INS` pairs earlier loses throughput on both
reconstruction loops. Separating the dependent lane writes recovers part
of that loss, but the original grouped-transfer schedule remains faster.
Grouped direct SIMD loads and scheduled `LDR Q` also lose to the winning
scalar schedule despite fewer instructions. These results support retaining
that schedule and sharing reconstruction; they do not establish a general
rule that scalar loads always beat SIMD loads.

Sixteen separate 3-second perf runs cover each target in both orders.
The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs without
multiplexing. Pooling the two complete middle 1-second intervals from each
order gives:

| Suffix | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| tbl | 1.1860 | 4.6376% | 0.0014% | 0.5201% |
| merge | 1.2061 | 3.9423% | 0.0018% | 0.5592% |
| merge-simd | 0.7923 | 2.7735% | 0.5211% | 0.6839% |
| merge-ldr | 0.8826 | 3.7994% | 0.0015% | 0.5765% |
| merge-early | 1.1317 | 3.6993% | 0.0017% | 0.6449% |
| early | 1.1188 | 4.3749% | 0.0013% | 0.5420% |
| merge-lanes | 1.1674 | 3.8158% | 0.0018% | 0.6350% |
| lanes | 1.1511 | 4.5010% | 0.0014% | 0.5701% |

The winning shared loop removes 43 instructions per 128 bytes (6.01%),
keeps the same multiplication count and improves IPC. SIMD interlocks
also decrease. Event categories can overlap and do not form a complete
cycle breakdown. Compare throughput alongside IPC: a multi-register load
counts as one instruction despite requiring several transfers.

Prefer `ghash-neon-8x-k32-merge` for bulk input on this A53. These are
userspace GHASH measurements with key setup excluded, not end-to-end GCM
or kTLS throughput. The comparison drivers remain explicitly selected
below stock priorities.

### Validation and reproduction

- GCC 14.2 userspace and external ARM64 module builds pass with warnings
  as errors; strict code-style checks pass.
- All eight targets pass 80,640 native and 80,640 user-mode Cortex-A53
  QEMU TCG comparisons through 131,201 bytes, covering offsets, continuing
  state, guard pages and ABI preservation.
- Independent assembly fixtures pass 1,536 cases per endianness, with
  three keys, four offsets, nonzero initial state and up to 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with full crypto selftests and KASAN passes
  63,280 direct-shash comparisons and 399 benchmark rows across the seven
  new drivers. Coverage includes update/final, export/import, partial
  tails, large updates and SG chunks of 17 and 1440 bytes.
- All 720 native timing rows and 16 PMU runs pass verification. Capture
  hashes, byte accounting and counter scheduling were checked. An earlier
  six-target run independently showed the same ordering of those targets.

Native sources, executable and captures use the `lanes-` prefix under
`/tmp/ghash-test`; the executable is `lanes-final`. The earlier six-target
captures use `merge-`. No kernel module was loaded on the device, and no
OpenWrt package or firmware image was built. The
[userspace commands](../../utils/ghash-bench-userspace/README.md#shared-k32-reconstruction-and-lane-scheduling-release-10)
select all eight targets.

## Fewer Karatsuba splits (release 31)

Five new eight-block comparisons independently remove the two Karatsuba
splits used by `ghash-neon-8x-k32-merge`:

- `ghash-neon-8x-k32-no128` computes four direct 64-bit products instead
  of the outer three-product Karatsuba formula. It retains the inner
  64-to-32-bit split. The two direct cross-products share byte-diagonal
  accumulators, so their sum is reconstructed once across all eight blocks.
- `ghash-neon-8x-k32-no64` retains the outer split, but computes the four
  direct 32-bit products for each 64-bit product. It reuses the same loaded
  key vectors for the low/high and swapped-input products, avoiding the
  inner XOR operands and their key-table loads.
- `ghash-neon-8x-k32-no128-no64` combines those changes. Its eight-block
  loop uses direct products at both levels.
- `ghash-neon-8x-k32-no64-early` and
  `ghash-neon-8x-k32-no128-no64-early` prepare the swapped 32-bit input
  with `REV64 .4s` before the low/high products. This moves it away from
  the consuming `TBL` without changing the instruction count.

All variants retain the existing 1120-byte context and four-block fallback.
The fallback still uses Karatsuba for calls shorter than eight blocks and
for remaining tails. No input rereads, scratch stores, register spills or
additional precomputed key data are introduced. All 19 pre-existing K32
functions retain identical instruction encodings. New driver priorities
are 48 through 44, with explicit selection below stock GHASH.

| Suffix after `ghash-neon-8x-k32-` | 128-bit split | 64-bit split | Instructions / 128 B | Byte PMULL/PMULL2 | Scalar key loads | Key bytes loaded |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `merge` (control) | Karatsuba | Karatsuba | 673 | 147 | 108 | 864 |
| `no128` | Direct | Karatsuba | 859 | 195 | 144 | 1152 |
| `no64` | Karatsuba | Direct | 676 | 195 | 72 | 576 |
| `no128-no64` | Direct | Direct | 864 | 259 | 96 | 768 |
| `no64-early` | Karatsuba | Direct | 676 | 195 | 72 | 576 |
| `no128-no64-early` | Direct | Direct | 864 | 259 | 96 | 768 |

Counts are for the little-endian hot loop, including loop control and the
three PMULLs used by field reduction. Setup and tails are excluded from
instruction counts but included in timing. Key-byte counts describe load
instructions, not DRAM traffic. The shared context retains tables needed
by other variants and the fallback even when a particular hot loop does
not use all of them.

### Cortex-A53 results

All six targets run on CPU3 at 1.2 GHz with the performance governor,
GCC 14.2 (`-O2 -g -Werror -static`), aligned input and seed 1708171853.
Each cell is the median of ten 300 ms trials: five in each algorithm/size
order, with 100 ms warmup per invocation. Key setup, input preparation
and verification are untimed. Rates are payload Gbit/s from the userspace
block entry points.

| Bytes | merge | no128 | no64 | no128-no64 | no64-early | no128-no64-early |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 1.768646 | 1.768631 | 1.772997 | 1.773339 | 1.772503 | 1.773135 |
| 112 | 1.373635 | 1.373675 | 1.373740 | 1.373751 | 1.374064 | 1.373503 |
| 128 | 2.081270 | 1.681268 | 1.930124 | 1.549613 | 1.967498 | 1.581524 |
| 144 | 1.789455 | 1.514341 | 1.684912 | 1.414228 | 1.709104 | 1.437666 |
| 256 | 2.144479 | 1.721957 | 1.981516 | 1.582378 | 2.021836 | 1.615870 |
| 1440 | 2.154932 | 1.736539 | 1.991345 | 1.595194 | 2.028856 | 1.628006 |
| 16384 | 2.207595 | 1.763121 | 2.031412 | 1.613885 | 2.072969 | 1.649618 |
| 16400 | 2.203578 | 1.758917 | 2.028927 | 1.612166 | 2.069313 | 1.647012 |
| 65536 | 2.193252 | 1.753824 | 2.019310 | 1.606959 | 2.061374 | 1.641482 |

Keeping both Karatsuba splits wins at every tested size of 128 bytes or
more: even the slowest control trial exceeds the fastest trial of every
new variant at each of those sizes. The best new schedule, `no64-early`,
reaches 2.061374 Gbit/s at 64 KiB, 6.01% below the 2.193252 Gbit/s control.
Removing only the outer split loses 20.04%; removing both loses 26.73%,
or 25.16% with the earlier shuffle. Inputs below 128 bytes share the
unchanged fallback and do not exercise these differences.

Twelve separate 3-second perf runs cover each target in both orders.
The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs without
multiplexing. Pooling the two complete middle 1-second intervals from each
order gives:

| Suffix | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| merge | 1.2039 | 3.9353% | 0.0018% | 0.7406% |
| no128 | 1.2302 | 3.7231% | 0.0015% | 0.4867% |
| no64 | 1.1159 | 3.6314% | 0.0014% | 0.5141% |
| no128-no64 | 1.1342 | 3.4129% | 0.0011% | 0.4038% |
| no64-early | 1.1381 | 1.6834% | 0.0014% | 0.5551% |
| no128-no64-early | 1.1584 | 1.3406% | 0.0011% | 0.4309% |

Removing the inner split keeps the total instruction count almost equal
(676 versus 673) and cuts key loads by one third, but increases byte
PMULLs from 147 to 195. It replaces some scalar work that could overlap
SIMD execution with additional SIMD work. Its IPC falls despite the
smaller key-load count. Preparing the swapped input earlier improves
throughput by 2.08% and reduces measured SIMD interlocks from 3.63% to
1.68% of cycles, but still loses to the original loop. The PMU categories
can overlap and are not a complete attribution of execution time.

Removing the outer split has slightly higher IPC than the control but
executes substantially more instructions, so it is slower. These tests
favor retaining both levels in `ghash-neon-8x-k32-merge` on this A53.
The five direct-product implementations remain explicitly selectable for
comparison; these measurements do not establish the best choice for a
CPU with different execution resources.

### Validation and reproduction

- GCC 14.2 userspace and external ARM64 module builds pass with warnings
  as errors; strict code-style checks pass.
- All six targets pass 60,480 native and 60,480 user-mode Cortex-A53
  QEMU TCG comparisons through 131,201 bytes, covering offsets, continuing
  state, guard pages and ABI preservation.
- Independent assembly fixtures pass 1,152 cases per endianness with
  three keys, four offsets, nonzero initial state and up to 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with full crypto selftests and KASAN passes
  45,200 direct-shash comparisons and 285 benchmark rows across the five
  new drivers. Coverage includes update/final, export/import, partial
  tails, large updates and SG chunks of 17 and 1440 bytes.
- All 540 native timing rows and 12 PMU runs pass verification. Capture
  hashes, byte accounting and counter scheduling were checked.

Native sources, executable and captures use the `split-` prefix under
`/tmp/ghash-test`; the executable is `split-final`. No kernel module was
loaded on the device, and no OpenWrt package or firmware image was built.
These are userspace GHASH rates with key setup excluded. The
[userspace commands](../../utils/ghash-bench-userspace/README.md#fewer-karatsuba-splits-release-11)
select all six targets.

## Sixteen-block K32 aggregation (release 32)

Two new drivers extend the eight-block shared-reconstruction loop while
retaining both Karatsuba splits:

- `ghash-neon-16x-k32` computes two eight-block groups, accumulates their
  reconstructed low/high/cross products and performs one field reduction
  for all sixteen blocks. It uses no stack scratch or spills.
- `ghash-neon-16x-k32-merge` also shares the inner reconstruction across
  all sixteen blocks. It keeps the byte diagonals in registers and saves
  the first eight blocks' high halves and low-XOR-high operands in 128
  bytes of stack scratch. Four `STP Q` and four `LDP Q` instructions store
  and reload that data per iteration. The scratch is cleared before
  returning or entering the tail path.

Both consume 256 input bytes per loop, load each input byte once, retain
scalar key-load scheduling and use the existing eight-block winner for
short calls and remaining tails. The 1984-byte context extends the old
1120-byte prefix with rotated tables for H^9 through H^16. No old key
layout changes, and all 24 pre-existing K32 function encodings remain
identical. The new drivers have priorities 43 and 42, below stock GHASH.

For equal amounts of input, the little-endian hot loops execute:

| Implementation | Iterations / 256 B | Instructions / 256 B | Byte PMULLs | Scalar key loads | Scratch stores / reloads |
| --- | ---: | ---: | ---: | ---: | ---: |
| `8x-k32-merge` | 2 | 1346 | 294 | 216 | 0 / 0 B |
| `16x-k32` | 1 | 1327 | 291 | 216 | 0 / 0 B |
| `16x-k32-merge` | 1 | 1287 | 291 | 216 | 128 / 128 B |

Counts include loop control and reduction; setup, stack clearing and
fallbacks are excluded from these counts but included in timing. Key
loads transfer 1728 bytes per 256 input bytes in every implementation.
This is load-instruction traffic, not measured DRAM traffic. The fully
shared version removes 59 instructions per 256 bytes (4.38%) despite its
eight scratch-access instructions.

### Cortex-A53 results

Measurements use CPU3 at 1.2 GHz, the performance governor, GCC 14.2
(`-O2 -g -Werror -static`), aligned input and seed 1708171853. Each cell is
the median of ten 300 ms trials, five in each algorithm/size order, with
100 ms warmup per invocation. Key setup, input preparation and
verification are untimed. Rates are payload Gbit/s from the userspace
block entry points.

| Bytes | 8x shared | 16x, no spills | 16x fully shared | Fully shared vs 8x |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 1.768688 | 1.758609 | 1.752381 | -0.92% |
| 128 | 2.082991 | 2.080754 | 2.078449 | -0.22% |
| 240 | 1.664710 | 1.613386 | 1.639912 | -1.49% |
| 256 | 2.144846 | 2.187326 | 2.215936 | +3.31% |
| 272 | 1.964173 | 1.995288 | 2.015065 | +2.59% |
| 512 | 2.176864 | 2.220072 | 2.259568 | +3.80% |
| 1440 | 2.155444 | 2.184187 | 2.226545 | +3.30% |
| 16384 | 2.201396 | 2.249474 | 2.302620 | +4.60% |
| 16400 | 2.202426 | 2.241222 | 2.300757 | +4.46% |
| 65536 | 2.191650 | 2.232995 | 2.286045 | +4.31% |

The fully shared sixteen-block loop wins at every tested size of 256
bytes or more. At each of those sizes, every fully shared trial beats
every trial of either alternative. It reaches 2.286045 Gbit/s at 64 KiB,
4.31% above the eight-block control, and gains 4.60% at 16 KiB and 3.30%
at 1440 bytes. Sharing only field reduction reaches 2.232995 Gbit/s at
64 KiB, a smaller 1.89% gain. Inputs below 256 bytes use the existing
fallback; their measurements include an extra entry comparison/branch
and show a modest regression.

Six separate 3-second perf runs cover each target in both orders.
The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs without
multiplexing. Pooling the two complete middle 1-second intervals from
each order gives:

| Implementation | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| `8x-k32-merge` | 1.2047 | 3.9377% | 0.0018% | 0.6786% |
| `16x-k32` | 1.2100 | 3.6468% | 0.0015% | 0.7288% |
| `16x-k32-merge` | 1.2018 | 3.1745% | 0.0016% | 0.8650% |

IPC stays close to 1.20. The throughput gain accompanies fewer
instructions per byte rather than higher IPC. Full reconstruction sharing
reduces measured SIMD interlocks while increasing load interlocks
slightly. The saved arithmetic outweighs the scratch traffic on this
A53. These counter categories can overlap and do not form a complete
cycle breakdown.

Prefer `ghash-neon-16x-k32-merge` for bulk input on this A53 when the
larger key context and 128-byte stack scratch are acceptable. These are
userspace GHASH measurements with key setup excluded; they do not measure
end-to-end GCM or kTLS throughput or establish the best choice on other
cores.

### Validation and reproduction

- GCC 14.2 userspace and external ARM64 module builds pass with warnings
  as errors; strict code-style checks pass.
- All three targets pass 30,240 native and 30,240 user-mode Cortex-A53
  QEMU TCG comparisons through 131,201 bytes, including offsets,
  continuing state, guard pages and ABI preservation.
- Independent assembly fixtures pass 792 cases per endianness, with
  three keys, four offsets, nonzero initial state and up to 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests passes
  18,080 direct-shash comparisons and 156 benchmark rows across the two
  new drivers. Coverage includes update/final, export/import, partial
  tails, large updates and SG chunks of 17 and 1440 bytes.
- All 300 native timing rows and six PMU runs pass verification. Capture
  hashes, byte accounting and counter scheduling were checked.

Native sources, executable and captures use the `agg16-` prefix under
`/tmp/ghash-test`; the executable is `agg16-final`. No kernel module was
loaded on the device, and no OpenWrt package or firmware image was built.
The [userspace commands](../../utils/ghash-bench-userspace/README.md#sixteen-block-aggregation-release-12)
select the three targets.

## Four-block K32 operand reuse and scheduling (release 33)

Five explicitly selected drivers refine `ghash-neon-4x-k32-tbl` while
retaining both Karatsuba splits, the 688-byte key context and the existing
two-block fallback. They consume 64 input bytes per iteration, with no
stack scratch, spills, extra key data or input overreads:

- `ghash-neon-4x-k32-input` selects the opposite ZIP lanes instead of
  swapping each of four input blocks. Swapping the incoming digest once
  saves three instructions per iteration.
- `ghash-neon-4x-k32-cross` retains the low product's inner cross operand
  in the consumed digest register. The outer cross operand is the XOR
  of the low and high inner crosses, saving four instructions. In other
  words, for `C(x) = x_low32 XOR x_high32`,
  `C(L XOR H) = C(L) XOR C(H)`.
- `ghash-neon-4x-k32-tight` combines those two changes.
- `ghash-neon-4x-k32-tight-early` additionally prepares each inner cross
  before its low/high products, increasing the separation between
  operand preparation and use.
- `ghash-neon-4x-k32-pipe` also moves the digest swap before the input
  load and prepares the reduction constant before the final product
  reconstruction.

All 26 pre-existing K32 functions have identical instruction encodings
in both userspace and kernel objects. The new drivers have priorities
41 through 37, below stock implementations.

| Loop | Instructions / 64 B | Byte PMULL / PMULL2 | Scalar key loads | Stack accesses |
| --- | ---: | ---: | ---: | ---: |
| `tbl` | 367 | 75 | 54 | 0 |
| `input` | 364 | 75 | 54 | 0 |
| `cross` | 363 | 75 | 54 | 0 |
| `tight` | 360 | 75 | 54 | 0 |
| `tight-early` | 360 | 75 | 54 | 0 |
| `pipe` | 360 | 75 | 54 | 0 |

Counts include little-endian loop control and field reduction; setup and
tails are excluded from these counts but included in timing. The combined
versions remove seven instructions per iteration (1.91%).

### Cortex-A53 results

Measurements use CPU3 at 1.2 GHz with the performance governor and
GCC 14.2 (`-O2 -g -Werror -static`). Input is aligned and the seed is
1708171853. Each value is a pooled median of ten 300 ms trials, five in
forward and five in reverse algorithm/size order, with 100 ms warmup per
invocation. Key setup, input preparation and verification are untimed.
Rates are userspace GHASH payload Gbit/s:

| Bytes | `tbl` | `input` | `cross` | `tight` | `tight-early` | `pipe` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.801012 | 0.801318 | 0.800985 | 0.800734 | 0.800944 | 0.801167 |
| 32 | 1.176590 | 1.176837 | 1.181487 | 1.181416 | 1.176517 | 1.181286 |
| 64 | 1.774923 | 1.790382 | 1.810462 | 1.827007 | 1.822133 | 1.832032 |
| 80 | 1.451074 | 1.459869 | 1.465420 | 1.474360 | 1.477047 | 1.477030 |
| 112 | 1.378709 | 1.383874 | 1.386787 | 1.393000 | 1.394174 | 1.394421 |
| 128 | 1.868785 | 1.887403 | 1.903458 | 1.922722 | 1.922367 | 1.927122 |
| 256 | 1.920512 | 1.939017 | 1.953690 | 1.972477 | 1.975603 | 1.979616 |
| 1440 | 1.936014 | 1.953986 | 1.966484 | 1.985790 | 1.990926 | 1.989951 |
| 16384 | 1.970991 | 1.991870 | 2.002562 | 2.025493 | 2.032713 | 2.030498 |
| 16400 | 1.971491 | 1.989030 | 2.001548 | 2.023029 | 2.028753 | 2.028244 |
| 65536 | 1.964431 | 1.983028 | 1.994888 | 2.013627 | 2.023391 | 2.021621 |

Twelve separate three-second perf runs cover every target in both orders.
The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs without
multiplexing. The table pools the two complete middle one-second
intervals from each order. These interlock categories can overlap and
are not a complete cycle breakdown.

| Loop | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| `tbl` | 1.1768 | 5.1302% | 0.0016% | 0.5188% |
| `input` | 1.1788 | 5.1813% | 0.0017% | 0.4746% |
| `cross` | 1.1831 | 4.8885% | 0.0014% | 0.5154% |
| `tight` | 1.1846 | 4.9354% | 0.0014% | 0.5306% |
| `tight-early` | 1.1886 | 3.9616% | 0.0017% | 0.5350% |
| `pipe` | 1.1886 | 3.9618% | 0.0014% | 0.5481% |

At 64 KiB, `tight-early` reaches 2.023391 Gbit/s versus
1.964431 for `tbl`, a 3.00% gain. Its gains are
3.13% at 16 KiB and 2.84% at 1440 bytes.

Prefer `tight-early` for bulk four-block processing on this A53. Its
64-KiB sample range does not overlap the control's. The combined changes
improve both instruction count and IPC: SIMD interlocks decrease from
5.13% to 3.96% of cycles. The further `pipe` schedule reaches 2.021621
Gbit/s at 64 KiB, within the `tight-early` sample range. It provides no
clear bulk improvement, but improves 64-byte calls from 1.822133 to
1.832032 Gbit/s. Calls of 16 and 32 bytes use the identical fallback and
stay approximately unchanged. These are userspace GHASH measurements
with key setup excluded, not end-to-end GCM or kTLS measurements.

### Validation and reproduction

- GCC 14.2 userspace and external ARM64 module builds pass with warnings
  as errors; strict code-style checks pass.
- All six targets pass 60,480 native and 60,480 user-mode Cortex-A53 QEMU
  TCG comparisons through 131,201 bytes, including offsets, continuing
  state, guard pages and ABI preservation.
- Independent assembly fixtures pass 1,584 cases per endianness, with
  three keys, four offsets, nonzero initial state and up to 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests passes
  45,200 direct-shash comparisons and 390 benchmark rows across the five
  new drivers. Coverage includes update/final, export/import, partial
  tails, large updates and SG chunks of 17 and 1440 bytes.
- All 660 native timing rows and twelve PMU runs pass verification.
  Capture hashes, byte accounting and counter scheduling were checked.

Native sources, executable and captures use the `fourp-` prefix under
`/tmp/ghash-test`; the executable is `fourp-final`. No kernel module was
loaded on the device, and no OpenWrt package or firmware image was built.
The [userspace commands](../../utils/ghash-bench-userspace/README.md#four-block-k32-operand-reuse-release-13)
select all six targets.

## Compact-key single-block scheduling and unrolling (release 34)

Nine new explicitly selected drivers optimize the serial single-block
K32 implementation. Every driver retains the 16-byte persistent key
context: no powers of H, duplicated key rotations or stack scratch are
stored per transform. The assembly prepares nine key operands and their
rotations in registers once per call. Every input block is multiplied by
H and reduced before processing the next digest, even in the unrolled
loops. This is serial loop unrolling, not multi-block aggregation.

- `ghash-neon-k32-sched` schedules operand preparation, independent byte
  products and reconstruction together to hide SIMD dependencies.
- `ghash-neon-k32-u2` and `ghash-neon-k32-u4` unroll that schedule twice
  and four times to reduce loop-control overhead.
- `ghash-neon-k32-u2-load` and `ghash-neon-k32-u4-load` load the next
  block through two scalar registers during the current multiplication.
  Scalar byte reversal is interleaved with SIMD arithmetic; lane writes
  occur after the current input register's last use.
- `ghash-neon-k32-direct` reduces the three outer Karatsuba products
  directly, removing one instruction per block and shortening dependencies.
- `ghash-neon-k32-direct-u4-load` combines that reducer with four serial
  iterations and preloads within each group.
- `ghash-neon-k32-packed-sched` reschedules the older packed ninth-product
  experiment, which uses two fewer byte-PMULL instructions per block.
- `ghash-neon-k32-direct-stream` also carries preloaded input across
  unrolled groups. A four-iteration group requires at least five blocks;
  the final block never preloads beyond the supplied input.

The new priorities are 36 through 28, below stock implementations. The
original two K32 functions have identical instruction encodings in both
userspace and kernel objects. There are no stack accesses in any of the
assembly functions; the userspace ABI adapter still preserves registers
as before. Five fixed shared masks occupy 80 bytes of read-only data,
independent of the number of transforms.

### Code size and key size

All rows below use a 16-byte persistent key. Counts are for the emitted
little-endian assembly, including loop control. Function size includes
setup and tails, excluding alignment and the userspace ABI adapter.
Unrolling trades shared instruction memory for less work per iteration;
it does not increase per-key memory.

| Variant | Bytes per main iteration | Instructions | Byte PMULL / PMULL2 | Function bytes |
| --- | ---: | ---: | ---: | ---: |
| `k32` | 16 | 114 | 23 | 560 |
| `k32-packed` | 16 | 115 | 21 | 572 |
| `k32-sched` | 16 | 114 | 23 | 560 |
| `k32-u2` | 32 | 227 | 46 | 1480 |
| `k32-u4` | 64 | 451 | 92 | 2376 |
| `k32-u2-load` | 32 | 231 | 46 | 1496 |
| `k32-u4-load` | 64 | 463 | 92 | 2424 |
| `k32-direct` | 16 | 113 | 23 | 556 |
| `k32-direct-u4-load` | 64 | 459 | 92 | 2404 |
| `k32-packed-sched` | 16 | 115 | 21 | 572 |
| `k32-direct-stream` | 64 | 463 | 92 | 2888 |

### Cortex-A53 measurements

CPU3 runs at 1.2 GHz with the performance governor. GCC 14.2 builds use
`-O2 -g -Werror -static`. Input is aligned and the seed is 1708171853.
Each cell is the median of ten 300 ms trials: five in forward and five
in reverse algorithm/size order, with 100 ms warmup per invocation.
Key setup, input preparation and verification are outside timing; entry
setup and serial tails remain included. Rates are GHASH payload Gbit/s,
not complete AES-GCM or kTLS throughput.

| Variant | 16 B | 32 B | 64 B | 128 B | 1440 B | 16 KiB | 64 KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `orig-pmull13` | 0.854846 | 0.971642 | 1.042612 | 1.082037 | 1.120815 | 1.124726 | 1.123383 |
| `k32` | 0.899879 | 1.037402 | 1.122819 | 1.170267 | 1.218055 | 1.223869 | 1.221927 |
| `k32-packed` | 0.873796 | 1.009934 | 1.094908 | 1.143299 | 1.189752 | 1.194668 | 1.193417 |
| `k32-sched` | 0.916563 | 1.059028 | 1.148456 | 1.199296 | 1.247280 | 1.253950 | 1.252293 |
| `k32-u2` | 0.910559 | 1.054927 | 1.148237 | 1.201333 | 1.252285 | 1.259005 | 1.256991 |
| `k32-u4` | 0.910680 | 1.054902 | 1.150071 | 1.204231 | 1.255372 | 1.260913 | 1.259847 |
| `k32-u2-load` | 0.910433 | 1.073323 | 1.170199 | 1.225838 | 1.278982 | 1.284802 | 1.283567 |
| `k32-u4-load` | 0.910578 | 1.055360 | 1.184156 | 1.240421 | 1.294892 | 1.301569 | 1.298766 |
| `k32-direct` | 0.932728 | 1.081048 | 1.174558 | 1.228146 | 1.279165 | 1.285228 | 1.283696 |
| `k32-direct-u4-load` | 0.927217 | 1.077219 | 1.212152 | 1.271494 | 1.328269 | 1.335652 | 1.334005 |
| `k32-packed-sched` | 0.894544 | 1.037080 | 1.127760 | 1.178835 | 1.227561 | 1.233627 | 1.231855 |
| `k32-direct-stream` | 0.921383 | 1.092486 | 1.205002 | 1.273863 | 1.343723 | 1.349824 | 1.348656 |

Twenty-four separate three-second perf runs cover all twelve targets in
both orders. The group `{cycles:u,instructions:u,re6:u,re5:u,re7:u}` runs
without multiplexing. Each row pools the two complete middle one-second
intervals from each order. Interlock categories can overlap and do not
form a complete cycle breakdown. The final column estimates cycles per
16 bytes from the 64-KiB trials' process CPU time and 1.2-GHz clock; it is
not a per-loop PMU measurement.

| Variant | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles | Estimated CPU cycles / 16 B |
| --- | ---: | ---: | ---: | ---: | ---: |
| `orig-pmull13` | 0.9763 | 4.4049% | 0.0001% | 0.1558% | 136.48 |
| `k32` | 0.9102 | 8.7828% | 0.0004% | 0.1815% | 125.49 |
| `k32-packed` | 0.8966 | 10.1350% | 0.0004% | 0.1944% | 128.47 |
| `k32-sched` | 0.9326 | 6.5449% | 0.0003% | 0.1788% | 122.44 |
| `k32-u2` | 0.9324 | 6.5721% | 0.0003% | 0.1738% | 121.98 |
| `k32-u4` | 0.9277 | 6.5828% | 0.0003% | 0.2154% | 121.69 |
| `k32-u2-load` | 0.9686 | 5.4513% | 0.0003% | 0.1914% | 119.47 |
| `k32-u4-load` | 0.9828 | 4.8825% | 0.0003% | 0.2160% | 118.02 |
| `k32-direct` | 0.9476 | 5.0316% | 0.0003% | 0.1991% | 119.49 |
| `k32-direct-u4-load` | 1.0000 | 3.2684% | 0.0005% | 0.2043% | 114.97 |
| `k32-packed-sched` | 0.9258 | 7.2454% | 0.0003% | 0.1624% | 124.49 |
| `k32-direct-stream` | 1.0197 | 2.6442% | 0.0005% | 0.2138% | 113.69 |

For bulk input, prefer `ghash-neon-k32-direct-stream`: 1.348656 Gbit/s
at 64 KiB, up 10.37% from the previous K32 loop and
20.05% from the original multiplication with the 13-instruction reducer.
The bulk sample ranges do not overlap either control's. IPC improves
from 0.9102 to 1.0197 relative to K32, and SIMD interlocks fall from
8.78% to 2.64% of cycles. The packed variant remains slower despite
executing fewer PMULLs.

`ghash-neon-k32-direct` is the smaller choice: 556 bytes of assembly and
1.283696 Gbit/s at 64 KiB. It also wins the 16-byte case.
`direct-u4-load` wins at 64 bytes; `direct-stream` wins at 32 bytes and
from 128 bytes upwards in this matrix, but grows the function to 2888 bytes. All keep
exactly the same 16-byte key context. Earlier four-block aggregation is
still faster in bulk, at the cost of a larger key context; that earlier
measurement was a separate run.

### Existing implementations with compact aggregation tables

Precomputed powers do not inherently need our larger byte-PMULL operand
tables. Linux 6.18's ARM64 AES-GCM path stores H through H^4 in 64 bytes,
plus 16 bytes of raw H for fallback (80 bytes total). The same layout is
visible as `be128 k; u64 h[4][2];` in
[`struct arm_ghash_key` on the Linux master branch](https://github.com/LGA1150/linux/blob/master/arch/arm64/crypto/ghash-ce-glue.c).
OpenSSL's ARM64 four-block path
reads 96 bytes containing four powers and packed Karatsuba cross terms;
its shared table reserves 256 bytes and current initialization also
computes powers through H^8. Linux 6.18's x86 VAES/VPCLMUL implementation
stores sixteen powers in 256 bytes, with another 48 bytes of zero padding.

Sources: [Linux ARM64 setkey](https://github.com/torvalds/linux/blob/v6.18/arch/arm64/crypto/ghash-ce-glue.c),
[OpenSSL ARM64 GHASH](https://github.com/openssl/openssl/blob/master/crypto/modes/asm/ghashv8-armx.pl),
[Linux x86 AES-GCM](https://github.com/torvalds/linux/blob/v6.18/arch/x86/crypto/aes-gcm-avx10-x86_64.S).
The ARM64 examples use optional 64-bit PMULL. A byte-PMULL implementation
could store only the powers and construct the rotated operands during
update, trading extra instructions for a smaller key context. These
single-block variants avoid storing key powers altogether.

### Validation and reproduction

- Warning-clean GCC 14.2 userspace and external ARM64 module builds;
  strict code-style checks pass.
- Twelve targets pass 120,960 native and 120,960 user-mode Cortex-A53
  QEMU TCG comparisons through 131,201 bytes, including offsets,
  continuing state, guard pages and ABI preservation.
- Independent assembly fixtures pass 3,168 cases per endianness with
  three keys, four offsets, nonzero initial state and up to 4,097 blocks.
- Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests passes
  81,360 direct-shash comparisons and 702 benchmark rows across all nine
  new drivers. Tests cover update/final, export/import, partial tails,
  large updates, and SG chunks of 17 and 1440 bytes. One initial guest
  timed out allocating a generic GCM selftest dependency during parallel
  startup; its identical image passed on retry.
- All 840 native timing rows and 24 PMU runs pass verification, capture
  hashes, byte-accounting and counter-scheduling checks.
- A short `-a all` run selects and verifies all 72 implementations.

Sources, executable, captures and validation logs use the `singleopt-`
prefix under `/tmp/ghash-test`; the executable is `singleopt-final`.
No kernel module was loaded on the device, and no OpenWrt package or
firmware image was built. The [userspace commands](../../utils/ghash-bench-userspace/README.md#compact-key-single-block-comparison-release-14)
select all twelve targets.

## Four-block aggregation with an 80-byte key (release 35)

`ghash-neon-4x-k32-compact` uses the same persistent layout as
[`struct arm_ghash_key`](https://github.com/LGA1150/linux/blob/master/arch/arm64/crypto/ghash-ce-glue.c):

```c
struct ghash_neon_compact_key {
	be128 k;
	u64 h[4][2];
};
```

The raw H occupies 16 bytes; the four reflected powers H through H^4
occupy another 64. Compile-time assertions check the 80-byte size and
16-byte powers offset. `setkey` computes the powers once. No rotated
operand tables or separate tail context persist with the key.

For calls containing at least four blocks, assembly builds a 432-byte
K32 operand table on the stack using the four powers. It then executes
the same tight/early four-block loop as the expanded-key control:
360 instructions and 75 byte PMULL/PMULL2 operations per 64 input bytes.
The temporary table is reused throughout that call and explicitly cleared
before return. Calls below four blocks and final one-to-three-block tails
use the single-block direct/stream loop with H. Each aggregated group
computes `(Y xor X1)*H^4 xor X2*H^3 xor X3*H^2 xor X4*H`, then reduces.

Persistent storage falls from **688 to 80 bytes**, saving 608 bytes (88.37%)
per key. Temporary stack use is **432 bytes per active assembly call**;
this is not an 80-byte total working set. The assembly function is 1852
bytes, excluding its shared single-block tail and fixed masks. All 31
pre-existing K32 function encodings remain unchanged.

### Cortex-A53 measurements

CPU3, 1.2 GHz, performance governor; static GCC 14.2 userspace executable.
Each cell pools ten 300 ms trials from forward and reverse orders, with
100 ms warmup per invocation. Key setup and verification are untimed.
Temporary-table preparation, clearing and serial tails are timed. These
rates measure GHASH payload, not complete AES-GCM or kTLS.

| Input bytes | Expanded 4x, 688 B | Compact 4x, 80 B | Single block, 16 B |
| ---: | ---: | ---: | ---: |
| 16 | 0.800603 | 0.910119 | 0.921452 |
| 32 | 1.176735 | 1.085231 | 1.092331 |
| 64 | 1.821429 | 1.318630 | 1.205192 |
| 80 | 1.477024 | 1.223075 | 1.235856 |
| 96 | 1.561517 | 1.244128 | 1.252884 |
| 128 | 1.922387 | 1.599291 | 1.273876 |
| 192 | 1.958067 | 1.721976 | 1.298488 |
| 256 | 1.976147 | 1.792096 | 1.311659 |
| 512 | 2.005175 | 1.904620 | 1.331087 |
| 1440 | 1.990947 | 1.948190 | 1.343185 |
| 16384 | 2.031006 | 2.029533 | 1.350634 |
| 65536 | 2.021292 | 2.020847 | 1.348397 |

Rates are Gbit/s. The userspace harness reserves a union large enough for
all comparison keys; the context sizes above are the actual kernel
Crypto API sizes. This measurement tests execution cost with a hot key,
not cache behavior across many simultaneously active keys.

| Variant | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| `4x-k32-tight-early` | 1.1885 | 3.9614% | 0.0017% | 0.5381% |
| `4x-k32-compact` | 1.1892 | 3.9666% | 0.0013% | 0.4656% |
| `k32-direct-stream` | 1.0200 | 2.6450% | 0.0005% | 0.1855% |

The non-multiplexed PMU group is
`{cycles:u,instructions:u,re6:u,re5:u,re7:u}`. Each row pools two complete
middle one-second intervals from each of two three-second runs.
Interlock categories can overlap; they are not a complete cycle breakdown.

The compact context preserves bulk performance: **2.020847 Gbit/s at
64 KiB versus 2.021292** for the expanded table, a 0.02% difference within
the observed sample ranges. At 16 KiB it reaches **2.029533 versus
2.031006 Gbit/s** (0.07% difference). IPC is effectively unchanged:
1.1892 versus 1.1885. The smaller persistent key is a useful tradeoff for
record-sized or larger updates on this A53.

Per-call setup matters for short updates: compact throughput is 2.15%
lower at 1440 bytes and 27.61% lower at 64 bytes than the expanded-key
control. Tail handling also differs: the compact version has a serial
tail, while the expanded control retains a two-block table. Compact
80- and 96-byte calls are about 1% slower than the best single-block
control. The four-block entry cutoff remains four blocks in this
comparison; it does not switch algorithms based on measured timings.

### Validation and reproduction

- GCC 14.2 userspace and external ARM64 module builds pass with warnings
  treated as errors. Strict kernel code-style checks pass.
- Three targets pass 30,240 native and 30,240 user-mode Cortex-A53 QEMU
  comparisons through 131,201 bytes, with offsets, continuing state,
  guard pages and ABI preservation.
- Independent assembly fixtures pass 792 cases per endianness, covering
  three keys, four offsets, nonzero initial states and up to 4097 blocks.
- Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests passes
  9040 direct-shash comparisons and 78 benchmark rows for the new driver,
  including update/final, export/import, partial tails, large updates,
  and SG chunks of 17 and 1440 bytes.
- All 360 native timing rows and six grouped PMU runs pass verification,
  capture hashes, byte accounting and counter scheduling checks.
- A short all-algorithm run selects and verifies all 73 implementations.

The explicit driver priority is 27, below stock implementations. Package
release 35 and userspace release 15 include the new implementation. Sources,
validation logs, exact measurement scripts and captures use the `compact4-`
prefix under `/tmp/ghash-test`; the executable is `compact4-final`.
No native kernel module was loaded and no OpenWrt package or image was
built. See the [userspace reproduction commands](../../utils/ghash-bench-userspace/README.md#four-block-aggregation-with-an-80-byte-key-release-15).

## Smaller stack for four-block aggregation (release 36)

Four variants keep the same 80-byte key layout while reducing the
432-byte temporary operand table used by `4x-k32-compact`:

- `stack288` stores the unrotated and byte-rotated operands, deriving the
  halfword rotation with `REV32` during multiplication.
- `stack144` stores only unrotated operands, deriving both rotations
  with `TBL` and `REV32` as the preceding product finishes.
- `regs32` retains all nine unrotated operand vectors in GPRs. It saves
  and restores x19-x22 in a 32-byte ABI save area; there is no stack key
  table and no key loads inside the loop.
- `regs0` retains six operand vectors in caller-saved GPRs and generates
  the three inner Karatsuba cross operands during the loop. It accesses
  no stack memory and uses neither x18 nor callee-saved GPRs.

The register variants still load H through H^4 once at entry. All four
preserve the four-block formula, 75 byte PMULL/PMULL2 operations per group,
shared reduction and single-block direct/stream tail. These are separate
comparison entry points; the existing implementations remain unchanged.
Names in the tables have the `ghash-neon-4x-k32-` prefix.

| Variant | Stack bytes | Function bytes | Instructions / 64 B loop | Key loads / loop |
| --- | ---: | ---: | ---: | ---: |
| `compact` | 432 | 1852 | 360 | 54 |
| `stack288` | 288 | 1664 | 333 | 36 |
| `stack144` | 144 | 1488 | 306 | 18 |
| `regs32` | 32 | 1380 | 278 | 0 |
| `regs0` | 0 | 1352 | 296 | 0 |

Stack sizes describe the raw GHASH assembly routine, separate from its
80-byte persistent key and unchanged caller/SIMD-entry overhead. Function
sizes and instruction counts use little-endian AArch64 code and exclude
shared tail code and fixed masks. Stack tables are cleared
before release; `regs32` restores the four saved registers before returning
or entering the serial tail. All 32 pre-existing K32 function encodings
remain identical.

### Cortex-A53 measurements

CPU3 at 1.2 GHz, performance governor; static GCC 14.2 userspace build.
Each cell pools ten 300 ms trials, five in forward order and five in
reverse algorithm/size order, with 100 ms warmup per invocation. Key setup
and verification are untimed; entry setup, temporary-table clearing,
register save/restore and serial tails remain timed. Rates are Gbit/s.

| Input bytes | `compact` | `stack288` | `stack144` | `regs32` | `regs0` |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 0.911058 | 0.910465 | 0.910746 | 0.910387 | 0.910363 |
| 32 | 1.084823 | 1.085814 | 1.085151 | 1.085247 | 1.085800 |
| 64 | 1.320069 | 1.385145 | 1.549637 | 1.562067 | 1.672695 |
| 80 | 1.224235 | 1.267960 | 1.376320 | 1.387779 | 1.452497 |
| 96 | 1.242650 | 1.280498 | 1.370313 | 1.382283 | 1.432963 |
| 128 | 1.599618 | 1.625763 | 1.757837 | 1.782425 | 1.835360 |
| 192 | 1.722980 | 1.724408 | 1.837844 | 1.870708 | 1.897154 |
| 256 | 1.791490 | 1.780676 | 1.884388 | 1.918317 | 1.929646 |
| 512 | 1.904497 | 1.870142 | 1.951802 | 1.993561 | 1.980937 |
| 1440 | 1.949093 | 1.898139 | 1.962182 | 2.005342 | 1.976506 |
| 16384 | 2.028641 | 1.965770 | 2.023851 | 2.071977 | 2.030684 |
| 65536 | 2.020697 | 1.958282 | 2.018517 | 2.067374 | 2.025780 |

**`regs0` removes all 432 stack bytes while preserving bulk throughput.**
At 64 KiB it measures 2.025780 versus 2.020697 Gbit/s; their sample ranges
overlap, so the small difference is not evidence of a bulk speedup.
At 1440 bytes it is 1.41% faster, and at 64 bytes it is 26.71% faster.
The six-operand setup also beats `regs32` on short aggregated calls.

**`regs32` is the throughput choice:** 2.067374 Gbit/s at 64 KiB, 2.31%
above the baseline, while reducing stack use by 92.59%. It is 2.14%
faster at 16 KiB and 2.89% faster at 1440 bytes. `stack144` approximately
preserves bulk performance, but `stack288` loses 3.09% at 64 KiB despite
executing fewer instructions than the baseline. Fewer instructions alone
do not determine A53 throughput.

| Variant | IPC | SIMD interlocks / cycles | AGU interlocks / cycles | Load interlocks / cycles |
| --- | ---: | ---: | ---: | ---: |
| `compact` | 1.1888 | 3.9653% | 0.0012% | 0.4984% |
| `stack288` | 1.0653 | 3.8416% | 0.0013% | 0.4368% |
| `stack144` | 1.0084 | 3.9553% | 0.0014% | 0.4431% |
| `regs32` | 0.9386 | 4.0548% | 0.0017% | 0.3689% |
| `regs0` | 0.9795 | 3.9723% | 0.0014% | 0.3725% |

The non-multiplexed PMU group is
`{cycles:u,instructions:u,re6:u,re5:u,re7:u}` at 64 KiB. Each row pools two
complete middle one-second intervals from each of two three-second runs.
Removing scalar loads reduces instruction count more than elapsed cycles,
so the faster register versions have lower IPC. Interlock categories can
overlap and do not provide a complete cycle breakdown.

These are hot-key GHASH measurements, not complete AES-GCM or kTLS. They
include per-call work but do not measure cache behavior across many keys.
The userspace harness reserves one union large enough for all comparison
keys; the 80-byte value is the actual kernel transform context size.

### Validation and reproduction

- Warning-clean GCC 14.2 userspace and external ARM64 module builds, and
  strict kernel code-style checks.
- 50,400 native and 50,400 user-mode Cortex-A53 QEMU comparisons across
  the baseline and four candidates: known answers, offsets, continuing
  state, guard pages, ABI preservation and lengths through 131,201 bytes.
- 1320 independent assembly cases per endianness, including three keys,
  four offsets, nonzero initial state and up to 4097 blocks.
- Kernel Cortex-A53 QEMU TCG with KASAN and full crypto selftests:
  36,160 direct-shash comparisons and 312 benchmark rows across the four
  new drivers, including update/final, export/import, partial tails,
  large updates and SG chunks of 17 and 1440 bytes.
- All 600 native timing rows and ten grouped PMU runs pass correctness,
  capture hashes, byte accounting and counter scheduling checks.
- A short all-algorithm run selects and verifies all 77 implementations.

Kernel package release 36 and userspace release 16 add explicit drivers
with priorities 26/25/24/23, below the stock implementations. Sources,
validation logs, exact measurement scripts and captures use the `stack4-`
prefix under `/tmp/ghash-test`; the executable is `stack4-final`.
No native kernel module was loaded and no OpenWrt package or image was
built. See the [userspace reproduction commands](../../utils/ghash-bench-userspace/README.md#smaller-stack-for-four-block-aggregation-release-16).
