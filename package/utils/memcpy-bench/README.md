# Userspace memcpy throughput benchmark

`memcpy-bench` measures single-threaded copies through the installed libc's
`memcpy`, independently of the crypto driver and test modules. It supports
copy blocks from **1 byte through 128 MiB** and streams through two separate,
page-aligned buffers. The default working set is **128 MiB per buffer**:
256 MiB of payload storage plus four pages of guards. Buffers are allocated
once and reused across sizes and trials.

The large working set makes small-block measurements useful for comparing
copy-call overhead and streaming memory throughput. Smaller working sets
can measure cache-resident copies. A working set exceeding the machine's
last-level cache is needed when investigating DRAM throughput.

## Build and run

Select the `memcpy-bench` package under **Utilities** in PonWrt. It installs
`/usr/bin/memcpy-bench` and requires no kernel module or non-libc dependency.
The source also builds independently, including with a target cross-compiler:

```sh
cc -O2 -std=c11 -Wall -Wextra -o memcpy-bench src/memcpy_bench.c
./memcpy-bench
```

Alternatively, run `make -C src CC=your-target-gcc`. No architecture-specific
copy implementation is bundled: the libc linked into the resulting binary
determines which implementation runs.

The default block sweep is:

```text
16,64,256,1K,1440,4K,16K,64K,256K,1M,4M,16M,64M,128M
```

Measure larger blocks only, pinned to a CPU available on the device:

```sh
memcpy-bench -s 1M,4M,16M,64M,128M -c 0 -d 500 -r 5
```

Compare sizes used by the EIP93 and kTLS tests against a large-copy baseline:

```sh
memcpy-bench -s 16,1440,16K,64K,128M -d 500 -r 5
```

Compare a smaller working set with streaming copies at the same block size:

```sh
memcpy-bench -s 1440,16K -W 256K -c 0
memcpy-bench -s 1440,16K -W 128M -c 0
```

`-W` is the size of **each** buffer, not their sum. It must be at least the
largest selected copy block; to reduce it below 128 MiB, select smaller
blocks with `-s` too. A 256 KiB buffer is only cache-resident if both buffers
and other active data fit the relevant cache.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `-s`, `--sizes` | The 14-size sweep above | Up to 32 comma-separated copy sizes, 1 byte..128 MiB |
| `-W`, `--working-set` | `128M` | Bytes per source/destination buffer, 1 byte..128 MiB |
| `-d`, `--duration` | `250` | Minimum milliseconds per timed trial, 1..60000 |
| `-w`, `--warmup` | `50` | Warmup milliseconds per size, 0..60000; zero disables |
| `-r`, `--runs` | `3` | Measured trials per size, 1..100 |
| `-c`, `--cpu` | `-1` | Pin to this logical CPU; -1 preserves current affinity |
| `-h`, `--help` | | Print usage |

Sizes accept decimal byte counts or case-insensitive binary suffixes `K`,
`KiB`, `M`, and `MiB`. For example, `128M` is 134,217,728 bytes. Invalid
sizes, extra arguments and unavailable CPU affinity requests fail explicitly.

## Measurement and interpretation

Each pass copies the entire working set in consecutive blocks. If its size
is not divisible by the selected block, the last copy is shorter. The clock
is checked once per full pass, so a slow pass can extend a trial beyond
`-d`. This ensures every trial reaches every page and avoids a clock read
on every tiny copy. Every copy goes through a volatile function pointer,
preventing the compiler from deleting repetitions, combining blocks or
substituting a builtin copy loop, including under LTO.

Allocation, page touching, source generation, destination poisoning and
data verification are outside the timed interval. The source has a fixed,
reproducible byte pattern. A preflight checks one full pass at each block
size; warmup and every measured trial also compare the entire destination
and both buffers' guards. Verification does not run between timed passes.
Timing uses `CLOCK_MONOTONIC_RAW`; process CPU time and page faults come
from `getrusage`. Buffers are not locked in RAM, so reported page faults
help identify runs affected by memory pressure.

CSV rows have this header:

```text
block_bytes,working_set_bytes,run,passes,copies,copied_bytes,elapsed_ns,seconds,gbps,mib_per_s,nominal_rw_gbps,cpu_s,minor_faults,major_faults
```

- `copied_bytes = passes * working_set_bytes` counts payload once.
- `copies = passes * ceil(working_set_bytes / block_bytes)` includes short
  final copies.
- `gbps = copied_bytes * 8 / elapsed_ns` is decimal gigabits per second.
  Compare this column with the EIP93 test's `mbps / 1000` or kTLS `gbps`.
- `mib_per_s` is payload throughput in binary MiB per second.
- `nominal_rw_gbps = 2 * gbps` accounts for one source read and one
  destination write. It is **not a measurement of DRAM bus traffic**.
- `cpu_s` is process user plus system CPU time for the trial, excluding
  initialization and verification. Small timing-bookend costs are included.
- `minor_faults` and `major_faults` count faults during the measurement.

Comment lines identify the machine/settings and report per-size minimum,
median, maximum and aggregate payload throughput. The aggregate divides
total bytes by total elapsed time. Even-sized run counts use the mean of
the two middle rates for the median. CSV rows are printed only after their
data and guard checks pass.

This measures CPU/libc copy throughput. Cache capacity, CPU frequency,
prefetching, write allocation, store policy and memory pressure affect the
result. Small blocks also include indirect-call and loop overhead. A large
working set reduces cache reuse but does not expose the DRAM controller's
traffic counters or establish peak bus bandwidth. The EIP93 NULL test uses
DMA with descriptor/IRQ overhead; a userspace `memcpy` result supplies a
CPU-copy comparison, not an equivalent device workload.

## Local validation

Host GCC builds pass with `-Wall -Wextra -Wformat=2 -Werror`, including
`-O3 -flto` and AddressSanitizer/UndefinedBehaviorSanitizer variants.
Eight successful invocations cover 112 CSV rows, the complete default
size sweep, 128 MiB blocks, odd working sets, partial final copies,
single-byte buffers, warmup, odd/even run counts and CPU affinity. CSV
byte/call accounting, rates and summaries are independently checked.

A counting `memcpy` interposer verifies that the LTO build performs every
reported copy plus preflight copies. Injected skipped copies, damaged data
and overwritten guards all fail before a timing row is printed. Thirty-three
expected failures also cover invalid arguments, unavailable affinity and
allocation failure under an address-space limit. These checks validate the
tool; host throughput is not an AN7581 measurement. No OpenWrt target or
firmware build was run locally.
