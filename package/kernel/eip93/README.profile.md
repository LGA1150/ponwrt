# Measuring EIP93 kTLS request costs

Driver release 37 adds patch 044 and the `eip93-measure-ktls` command.
Profiling is off by default and changes no cipher, DMA, queue or register
policy. It records aggregate skcipher counters without logging each request.
Test package release 9 also includes a separate
[aligned CTR throughput benchmark](../eip93-test/README.ctr-bench.md).
No OpenWrt target build was run locally.

For the next comparison on the tested Airoha device, keep input threshold
zero: a release-37 run with tail alignment off reported a plaintext
mismatch at input threshold `0x40`. Select the threshold separately before
capture; the runner only reads the registers. With release 37 loaded, run:

```sh
EIP93_REG_BASE=0x1fb70000 eip93-measure-ktls ./ktls-bench /tmp/eip93-profile-zero
```

The optional `EIP93_REG_BASE` enables read-only `devmem` snapshots of DMA
configuration and buffer thresholds. Omit it on another device, or supply
that device's register base. The runner never writes those registers.
Use a new output directory for each comparison. Keep the MTU, threshold,
other crypto settings and background workload fixed.

The runner executes TLS 1.2 RX with `-j 2 -p 0 -w 0 -r 5`, 4,096 records
of the benchmark's default 16,384-byte size. It runs tail alignment off/on
with profiling disabled, then on/off with profiling enabled. This gives
uninstrumented throughput controls as well as measurements of each path.
`RECORDS`, `RUNS` and `THREADS` override these defaults. Verbose skcipher
DMA logging is disabled during capture. The original profiling, minimum
size, tail-alignment and logging settings are restored on success or error.
The runner resets the aggregate counters before each benchmark process.

The output directory contains:

- `settings.txt`: original module parameters, kernel, loopback MTU and any
  requested register readbacks.
- `profile0-tail0.bench` and `profile0-tail1.bench`: throughput controls.
- `profile1-tail0.bench` and `profile1-tail1.bench`: instrumented benchmark
  output, including the usual verification and CPU-time columns.
- Matching `.stats` files: raw driver counters, including encryption and
  decryption separately. Encryption includes untimed ciphertext preparation.
- `summary.csv`: per-request decryption averages, with times in microseconds.
  A run can contain both legacy and CTR-SG rows; weight their averages by
  request count when combining them.

For device reports, retain `settings.txt`, all four `.bench` files,
the two instrumented `.stats` files and `summary.csv`. The first device
capture is analyzed below; QEMU timing is not an estimate of EIP93
performance.

## Counter meanings

Counters cover skcipher requests reaching the submission worker. Native
authenc, hashes, rejected enqueues and size-based software fallback are not
included. Totals are module-wide, separated by legacy/CTR-SG path and by
encrypt/decrypt. The default minimum size of 1,024 bytes excludes generic
GCM's 16-byte setup requests. Logical sizes include the GCM prefix; a
16,384-byte TLS record normally generates a 16,400-byte CTR request.

| Field | Meaning |
| --- | --- |
| `requests`, `errors` | Finished samples and nonzero driver completion statuses. |
| `bytes`, `src_nents` | Logical bytes and original source entries before any bounce. |
| `descriptors`, `hw_bytes` | Published descriptors and their processed bytes, including discarded tail bytes. |
| `sw_bytes` | Bytes assigned to CTR software boundary processing. |
| `src_copies`, `dst_copies` | Requests prepared with private source/output buffers. |
| `id_ns` | ID allocation, including waiting for request credits. |
| `prepare_ns` | Work after ID allocation until ready to submit, including validation, allocation, bouncing, planning and DMA mapping. |
| `plan_ns` | Subset of preparation: both CTR planning passes, including counter initialization; excludes allocation. |
| `map_ns` | Subset of preparation: source/output, SA and counter-state DMA mapping calls. |
| `submit_ns` | Ready-to-submit through the last descriptor's publication point, including ring-slot waits and descriptor construction. |
| `pending_ns` | Last descriptor's publication point to completion handling; includes hardware queueing, DMA and interrupt/tasklet delay. |
| `complete_ns` | DMA unmapping, any software boundary AES, copyback, IV update and cleanup, before the caller's callback. |
| `copy_ns` | Subset of completion: copying output into the caller's destination SG. |

All times are **elapsed monotonic nanoseconds**, including preemption and
waits. They are not CPU time or engine busy time. Waiting in the software
queue before the worker starts is not measured. Earlier descriptors may
execute while later descriptors are still being submitted. Summed request
times can exceed benchmark wall time because requests overlap. Do not add
`plan_ns` or `map_ns` to `prepare_ns`, or `copy_ns` to `complete_ns`.

Compare descriptors/request and source-copy fractions first. Then compare
preparation, its planning/mapping components, submission, pending and
completion. This separates extra descriptors from planning or copyback
cost without treating interrupt delay as accelerator execution time.
Clock reads and aggregation themselves add overhead; use the profiling-off
controls to assess its effect on throughput.

## Manual use

```sh
p=/sys/module/crypto_hw_eip93/parameters
echo 1024 > "$p/skcipher_profile_min_bytes"
echo 0 > "$p/skcipher_profile_stats"
echo 1 > "$p/skcipher_profile"
./ktls-bench -d rx -v 1.2 -j 2 -p 0 -w 0 -r 5
echo 0 > "$p/skcipher_profile"
cat "$p/skcipher_profile_stats"
```

`skcipher_profile_stats` returns one snapshot. Writing `0` resets it and
advances a generation; requests already being profiled at reset are
excluded when they finish. Disabling profiling affects new samples;
already-started samples still finish and are counted. The runner reads
after the benchmark has completed all requests. No reset or read changes
the cryptographic request itself. Requests that fail before path selection
are recorded in the legacy bucket.

The host GCC `W=1` build and strict code-style check pass. QEMU checks cover
disabled/filtered sampling, reset with an outstanding sample, invalid reset
values, disabling during a sample, duplicate completion suppression, 4,000
concurrent updates and maximum-size sysfs output. Driver-model tests verify
exact counts for the overlapping CTR replay, software boundary spans,
mapping/error cleanup, and the runner's settings restoration on success
and benchmark failure. Existing cipher and kTLS tests run with profiling
enabled; the model validates instrumentation and request behavior, not
hardware timing.

The profiled QEMU run passes 3,496 existing test cases, 4,326 focused probe
checks, 33 standalone benchmark runs and 28 existing kTLS selftests. The
capture runner adds four passing benchmark invocations and verifies its
failure path. All 43 package patches apply without offsets or fuzz and
reproduce the 18 tested driver source files.

## Device perf comparison at input threshold zero

The release-37 device report provides `eip93-tail0.bench`,
`eip93-tail1.bench` and corresponding `.report` files. Both captures use
input threshold zero, TLS 1.2 AES-GCM-128 RX, two threads, no prefill and
20 trials of 4,096 full-size records. All 40 trials verify plaintext,
covering 2.5 GiB across both settings. Only perf was run in this initial
capture; the later driver-stage measurements follow in the next section.

| Metric | Tail alignment off | Tail alignment on |
| --- | ---: | ---: |
| Median throughput (Gbps) | 0.952146 | 0.883307 |
| Aggregate throughput (Gbps) | 0.957664 | 0.880912 |
| Mean elapsed time per 64 MiB (s) | 0.560605 | 0.609449 |
| Mean summed RX thread CPU time (s) | 0.594014 | 0.601677 |
| Mean summed feeder CPU time (s) | 0.164765 | 0.159741 |
| Mean system busy time (s) | 1.182000 | 1.166500 |

Median throughput falls 7.23%; elapsed time rises 8.71%. RX thread CPU
time rises 1.29%, while whole-system busy time is approximately unchanged.
The reports contain about 12K/13K samples of `cpu-clock:k`, with no lost
samples. They are not hardware-cycle profiles. They cover the entire
benchmark process, including untimed ciphertext preparation and unrelated
machine activity, so their accounting interval differs from the CSV.

Aggregating self overhead across command names gives:

| Symbol | Off (% of all samples) | On (% of all samples) |
| --- | ---: | ---: |
| `default_idle_call` | 58.82 | 61.93 |
| `pmull_ghash_update_p8` | 19.90 | 19.25 |
| `__pi_memcpy_generic` | 3.92 | 2.70 |
| `__pi_dcache_clean_poc` | 0.92 | 0.48 |
| `eip93_get_descriptor` | 0.21 | 0.31 |
| `eip93_plan_ctr_sg` | not sampled | 0.10 |
| `eip93_ctr_sg_software` | not sampled | 0.16 |
| `aes_encrypt` | not sampled | 0.10 |

The displayed small percentages are rounded and some helpers are inlined;
absence or low self overhead does not bound inclusive execution cost.
Exclude only `default_idle_call` when interpreting active kernel samples,
not all `swapper` samples: EIP93 completion and GHASH also run in tasklet
context while the interrupted task is idle. GHASH represents approximately
48%/51% of non-idle kernel sampling, making it the largest sampled active
CPU consumer. The `_p8` routine is the NEON byte-polynomial GHASH path;
it is separate from EIP93's MD5/SHA/HMAC acceleration.

Worker-context memcpy overhead falls from about 0.90% to 0.08%, consistent
with avoiding source bounces. Copying and cache cleaning decrease even
after allowing for the longer enabled capture. The new planning and
boundary-processing functions appear, but these samples do not show a
large increase in total active CPU work. More CPU idle sampling does not
establish accelerator idle time or utilization. Increased descriptor
overhead or reduced overlap remains a hypothesis; request descriptor
counts and stage timings were still needed to distinguish them at this
point.

The separate `0x40` plaintext mismatch occurred with
`skcipher_tail_align=0` on release 37. That request path does not use the
CTR-SG planner or overlapping output. It is consistent with the previously
observed legacy DMA input-tail problem, but the failing request was not
captured here. Generic GCM hashes the original ciphertext before invoking
CTR decryption. Corruption confined to the accelerator's payload read or
output can therefore produce wrong plaintext without a bad-tag error;
the tag calculation need not see the same corrupted bytes. These passing
zero-threshold captures neither diagnose that failure nor validate the
default `0x40` threshold.

## Device stage counters at input threshold zero

The subsequent runner capture confirms `dma_cfg=0x00180006`,
`buffer_thresholds=0x00400000`, `aes_sw_fallback=N`,
`skcipher_partial_src=N` and loopback MTU 65,536. It uses TLS 1.2
AES-GCM-128 RX with two threads, 64 records per read, no prefill and five
64 MiB trials per setting. All 20 trials verify plaintext, totaling
1.25 GiB. The disabled-profile counters remain zero as expected.

| Throughput (Gbps) | Tail alignment off | Tail alignment on |
| --- | ---: | ---: |
| Profiling disabled, median | 0.947531 | 0.866330 |
| Profiling disabled, aggregate | 0.960979 | 0.872098 |
| Profiling enabled, median | 0.968660 | 0.882290 |
| Profiling enabled, aggregate | 0.955976 | 0.875978 |

The uninstrumented median falls 8.57% and aggregate throughput falls
9.25%. Profiling changes aggregate throughput by -0.52% with tail
alignment off and +0.44% with it on in these runs. This comparison does
not establish zero instrumentation cost, but it preserves the slowdown.

Each instrumented path records exactly 20,480 decrypt requests, zero
errors and 335,872,000 logical bytes: 16,400 bytes per request, including
the 16-byte GCM prefix. The separate 4,096 encrypt requests are ciphertext
preparation and are excluded from the RX averages below.

| Mean per decrypt request | Legacy | CTR-SG |
| --- | ---: | ---: |
| Original source entries | 2.778662 | 2.780957 |
| Hardware descriptors | 1.416309 | 2.775342 |
| Source-copy fraction | 61.04% | 0% |
| Destination-copy fraction | 100% | 100% |
| Software boundary bytes | 0 | 11.635156 |
| Discarded DMA tail bytes | 0 | 2.578174 |
| ID allocation (us) | 1.027817 | 1.129573 |
| Preparation (us) | 23.389677 | 12.507666 |
| Planning, within preparation (us) | 0 | 1.219568 |
| DMA mapping, within preparation (us) | 10.294075 | 5.871297 |
| Submission (us) | 0.656812 | 2.150963 |
| Pending completion (us) | 2084.359206 | 3466.913624 |
| Completion (us) | 35.524029 | 35.115767 |
| Copyback, within completion (us) | 27.631287 | 26.125998 |

The source shape is essentially unchanged. Avoiding source copies retains
more fragmentation: total descriptors rise from 29,006 to 56,839 (+95.96%),
while preparation falls 46.52%. Completion and copyback do not regress.
The measured non-pending phases sum to 60.60 versus 50.90 us per request;
they are elapsed phases, not a CPU-time measurement. Hardware processes
335,686,513 bytes in CTR-SG mode, while software handles 238,288 bytes;
their sum exceeds the logical byte count by only 52,801 discarded tail
bytes. Bulk extra padding or software encryption therefore does not
explain the slowdown in this capture.

Pending time grows 66.33%, but includes time behind earlier descriptors,
DMA, interrupts and tasklet delay. Faster preparation can also publish
requests earlier and increase their queue residence. Dividing summed
pending time by summed timed trial duration gives approximately 15.20
versus 23.17 requests awaiting completion on average. This is an estimate
from overlapping intervals, not a hardware queue-depth counter. It does
not mean AES execution became 66% slower. ID allocation and submission
averages show no large ID/ring-credit wait, and these counters exclude
software queue delay before worker entry.

The next optimization target is the additional descriptor and fragmented
DMA work. Each descriptor currently takes a ring lock and doorbell write,
and its result requires acknowledgement; the CTR planner also supplies
an independent counter state for each span. These measurements do not
separate that cost from the effects of direct unaligned source reads.
The output-copy path and planning are not the leading regression in this
capture.

A focused experiment would handle a separate one-block source segment
with the existing synchronous CTR helper, retaining hardware for bulk
spans. Generic GCM creates a separate 16-byte tag-mask prefix, so this
could avoid approximately one hardware descriptor per record where it
remains a separate DMA segment. It would move that AES block to software;
it is a proposed comparison, not current behavior or a measured speedup.
Descriptor publication/retirement batching is a separate candidate that
keeps those blocks in hardware. Neither change requires source bouncing
or a `crypto_engine` conversion.

The next device measurement establishes the aligned, single-entry CTR
throughput curve over different lengths and queue depths using
`eip93_ctr_bench.ko`. It excludes the GCM prefix descriptor, fragmented
sources and GHASH. The benchmark is independent of the proposed descriptor
optimizations above; driver behavior is unchanged.
