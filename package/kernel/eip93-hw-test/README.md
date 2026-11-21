# Standalone EIP93 hardware measurements

`kmod-eip93-hw-test` supplies `eip93_hw_test.ko`. It drives EIP93 MMIO,
command/result rings and the interrupt directly. It neither loads nor calls
`crypto_hw_eip93`, registers Crypto API algorithms, nor uses that driver's
queue, scatterlist handling, fallbacks or profiling code. The only crypto
dependency is the kernel's software AES library, used outside the timed
intervals to generate payloads and reference ciphertext.

The module measures **NULL-copy and AES-CTR**, with independent source and
destination byte offsets, lengths from 1 through 65536 bytes and up to 128
outstanding descriptors. NULL-copy provides a DMA/ring baseline for the
CTR comparison. This is a test module, not a DMAengine provider.

For a CPU-copy comparison, [memcpy-bench](../../utils/memcpy-bench/README.md)
streams libc `memcpy` calls through two 128 MiB userspace buffers, using
block sizes up to 128 MiB. Its payload `gbps` column uses the same byte
accounting as this test's `mbps / 1000`.

Release 2 programs and verifies settings after releasing PE reset, handles
PonWrt's generic-only Airoha DT nodes, and clears the result interrupt after
decrementing the result count. It uses automatic clock gating by default.
Release 3 sweeps AES-128, AES-192 and AES-256 by default. `key_bits` accepts
a single size or a comma-separated subset, for example `key_bits=192,256`.
The CSV `key_bits` column identifies each CTR result; NULL-copy runs once
per length/depth and reports key size zero.
Release 4 keeps PE/AES clocks forced on while saving, programming and
verifying registers. It switches to the selected measurement clock setting
only after setup verification, and restores the saved clock setting last.
This fixes the automatic-clock setup error in releases 2 and 3. Release 5
also fixes restoration: after reset, it explicitly releases both resets in
idle direct-host mode and verifies empty rings instead of requiring the
ring-reset bit to read back as one.
Release 6 accepts exact byte lengths, including partial CTR blocks, and adds
the default-off `dma_skip_cpu_sync` option for payload coherency experiments.
Release 7 adds configurable result-descriptor interrupt batching through
`result_batch` and `result_timeout`. The default remains immediate result
notification, matching the earlier measurements.
Release 8 adds optional IRQ/polling completion handling (`irq_poll=1`),
with bounded drain/refill rounds and a recheck when interrupts are rearmed.
It appends polling counters to the CSV. The default remains `irq_poll=0`.
Release 9 adds `dst_offset` to test unaligned destination DMA addresses.
It defaults to zero and is reported in the settings header and CSV rows.
Release 10 identifies source versus destination verification failures and
prints the first/last bad payload offsets, a bounded expected/actual window, and
the adjacent guards after synchronizing the mapping for CPU access.
Release 11 adds `src_offset`, independently configurable alongside
`dst_offset`. Both default to zero and are reported in settings, failure
diagnostics and CSV rows. The new CSV field is appended after `dst_offset`.

## Loading on the device

Select `kmod-eip93-hw-test` under Cryptographic API modules. It is separate
from `kmod-eip93` and `kmod-eip93-test`; all may remain installed. There is
no autoload entry or module device alias. The package supports little-endian
targets with device tree, matching the same EIP93 I/IE/IS/IES compatibles
as the current driver. The DT supplies the register resource and IRQ; no
physical address is hardcoded.

Stop the crypto benchmarks and unload the normal EIP93 driver first. If it
is still in use, `rmmod` fails and the following `insmod` is not run:

```sh
rmmod crypto_hw_eip93 &&
insmod eip93_hw_test mode=both lengths=16,64,256,1024,4096,16384,16400,65536 \
    depths=1,32 duration_ms=250 warmup_ms=50 runs=3 \
    dma_maxburst=6 input_threshold=0 output_threshold=64
```

On AN7581, release 5 verifies these settings before submitting any work:

```text
DMA=0x00180006 thresholds=0x00400000 endian@0x1d0=0x00e400e4 clocks=0x0
```

The remaining DMA configuration bits are preserved from the previous
configuration, so the full DMA value can differ on another platform.
Incorrect readbacks abort the test instead of timing unintended settings.
The header reports values verified with clocks enabled; the selected clock
setting is applied and checked separately before measurement begins.

The test claims the platform device, its MMIO region and its IRQ exclusively.
If the normal driver is still bound, insertion fails without touching its
registers. Binding failure, a hardware error, a timeout or a verification
failure makes insertion fail. Do not change registers with `devmem` during
the sweep.

Tests run during insertion. After a successful sweep, the device remains
bound to the test module, with the engine stopped, until it is unloaded:

```sh
dmesg | sed -n '/eip93-hw-test\|eip93_hw_test/p' > /tmp/eip93-hw-test.log
cat /sys/module/eip93_hw_test/parameters/rows
cat /sys/module/eip93_hw_test/parameters/requests
rmmod eip93_hw_test && modprobe crypto-hw-eip93
```

Log extraction includes any earlier runs still in the kernel log. Each
load has a settings header and final PASS/FAIL line. The normal driver
initializes its registers again when reloaded; reapply your usual settings
before subsequent kTLS measurements.

For separate, longer measurements, unload the test between invocations:

```sh
insmod eip93_hw_test mode=null lengths=16,64,256,1024,4096,16384,65536 \
    depths=1,32 duration_ms=1000 runs=5
rmmod eip93_hw_test
insmod eip93_hw_test mode=ctr direction=decrypt \
    lengths=16,64,256,1024,4096,16384,16400,65536 \
    depths=1,32 duration_ms=1000 runs=5
```

The CTR command now covers all three key sizes. Add `key_bits=128` to
reproduce the original single-key sweep, or `key_bits=192,256` to measure
only the two larger sizes. All selected sizes must be supported by the
hardware; otherwise insertion fails before the sweep. NULL-only runs do
not require hardware AES support; their payloads use software AES-128.

The normal driver must remain unloaded for both commands. Use the existing
[Crypto API CTR benchmark](../eip93-test/README.ctr-bench.md) with the driver
loaded to measure the complete driver path separately.

## TCP MSS-sized operations

Release 6 accepts exact byte lengths, including a partial final CTR block.
The buffer starts are aligned by default; the descriptor length, expected result,
guards and byte accounting use the requested length without rounding it up.
The original eight-size default sweep is unchanged.

These sizes cover common full-sized TCP payloads:

| Link MTU | IP | MSS without TCP options | Data with 12-byte timestamp options |
| --- | --- | ---: | ---: |
| 1500 (Ethernet) | IPv4 | 1460 | 1448 |
| 1500 (Ethernet) | IPv6 | 1440 | 1428 |
| 1492 (PPPoE) | IPv4 | 1452 | 1440 |
| 1492 (PPPoE) | IPv6 | 1432 | 1420 |

The timestamp column is the effective data payload; the advertised MSS
itself excludes TCP options. Additional IP/TCP options can reduce the payload.
With the normal driver unloaded, run:

```sh
insmod eip93_hw_test lengths=1420,1428,1432,1440,1448,1452,1460
```

Unload a previous test instance before inserting it again. This produces
294 timed rows: seven lengths, depths 1/32, NULL-copy plus all three AES
key sizes in both directions, and three trials. Requested warmup/timing
windows total 78.4 seconds, with setup and final draining adding wall time.
Each operation is contiguous; this does not model a TLS record split
across TCP segments. The [Crypto API benchmark](../eip93-test/README.ctr-bench.md#tcp-mss-sized-operations)
accepts the same lengths for comparison with the normal driver loaded.

## Destination address alignment

`dst_offset` shifts the destination DMA start by the specified number of
bytes from its usual aligned position. It accepts 0 through `PAGE_SIZE - 1`
(0..4095 on a kernel with 4 KiB pages). The source stays aligned by default, and
descriptor lengths and payload byte accounting remain exact. The option
applies to NULL-copy and CTR at every selected key size and queue depth.

Compare word-aligned controls with offsets 1, 2 and 3, with the normal
driver and any previous test instance unloaded:

```sh
for offset in 0 1 2 3 4; do
    insmod eip93_hw_test mode=both dst_offset="$offset" \
        lengths=1,4,16,1440,16384,65536 depths=1,32 \
        duration_ms=50 warmup_ms=0 runs=1 || exit 1
    rmmod eip93_hw_test || exit 1
done
```

This covers NULL-copy and all three CTR key sizes in both directions. A
failure stops the loop and logs the effective source/destination DMA
addresses. Run the remaining offsets separately if one fails; use
`mode=null` or `mode=ctr` to isolate an operation.

The module maps the whole allocation with an aligned base and length for
normal cache maintenance, then puts the shifted address into the hardware
descriptor. It does not realign the destination through a private bounce
buffer. Guards cover every byte before and after the exact output region,
including the added offset space. Leave `dma_skip_cpu_sync=0` to test
address alignment independently of coherency.

### Source and destination combinations

Release 11 adds `src_offset`, with the same 0..PAGE_SIZE-1 range and default
zero. Both offsets are relative to an aligned payload start in their own
allocations. Source and destination remain separate allocations, including
when their offsets match. Payload generation, references and guards follow
the shifted data pointers, and descriptors use the corresponding DMA
addresses. Whole-allocation mappings remain aligned for cache maintenance;
sources use `DMA_TO_DEVICE` and destinations use `DMA_BIDIRECTIONAL`.

To test all 16 combinations of word-alignment residues, load each pair
separately. Running each length separately also lets the loop continue
past a failing short transfer. With the normal driver and any previous
test instance unloaded:

```sh
for length in 2 3 4 5 16 1024; do
    for src in 0 1 2 3; do
        for dst in 0 1 2 3; do
            if insmod eip93_hw_test mode=null lengths="$length" \
                src_offset="$src" dst_offset="$dst" depths=1 \
                input_threshold=8 duration_ms=1 warmup_ms=0 runs=1; then
                rmmod eip93_hw_test || exit 1
            fi
        done
    done
done
```

This includes aligned/aligned, aligned/unaligned, unaligned/aligned, and
unaligned pairs with matching or different offsets. Use `mode=ctr` and
`key_bits=128` to repeat the matrix with AES-128 CTR in both directions.
Omitting `key_bits` covers AES-128/192/256. Use `depths=32` for a separate
queued comparison. Offset 4 can serve as a further word-aligned control.

The register settings and each source/destination offset pair are printed
before testing. Failed insertions stop that case and release the device,
allowing subsequent combinations to run. Keep `dma_skip_cpu_sync=0`.

The first AN7581 offset sweep passes all 280 rows and 1,760,020 timed
requests at offsets 0/1/2/3/4, lengths 16/1440/16384/65536 and depths 1/32.
It covers NULL-copy and all CTR key sizes in both directions with burst 6,
thresholds 0/64 and normal cache synchronization. At 64 KiB/depth 32,
NULL-copy remains about 1.710 Gbps and AES-128 CTR about 1.316 Gbps across
these offsets. Those lengths are all multiples of 16; offsets 1/2/3 still
exercise both unaligned start and end addresses.

### Observed failures on AN7581

Passing the larger-length sweep does not establish support for arbitrary
unaligned destinations. The subsequent captures show two patterns:

- At length 1024 and destination offset 2, NULL-copy fails a depth-32
  preflight after depth 1 passes. Release 10 locates one failure at byte
  1022: the expected final bytes `1a c2` become `c2 c2`, with intact guards
  and source verification passing. Raising the input threshold from 0 to 8
  is reported to pass this replay. That is a threshold-sensitive observation,
  not a verified fix for every length/alignment. Queue depth also changes
  buffer addresses and per-slot data, so a concurrency dependency is not
  established. A separate offset-1 sweep at lengths 256/1024 passes all
  28 rows and 215,012 requests.
- Two-byte NULL-copy fails at odd destination offsets at depth 1, with
  either input threshold 0 or 8. At threshold 8, offsets 0/2/4 pass. Offset 1
  leaves the payload at `a5 a5`; offset 3 produces `a5 02` and corrupts three
  suffix-guard bytes to `00 80 00`. Thus guard preservation from the larger
  sweep does not generalize to this short transfer.

The three-byte NULL-copy at offset 3 also fails and overwrites two suffix
bytes. A four-byte copy writes only its first byte; five-, six- and ten-byte
copies pass in these captures. Length 4 supplies a complete source word,
whereas length 5 first includes a complete aligned destination word. This
narrows the failing geometry without identifying a particular RTL state.

A controlled sequence confirms that the three-byte failure contains the
preceding successful request's result-length/ownership word. Both lengths
run in the same module invocation, at depth 1, destination offset 3, burst 6,
input/output thresholds 8/64, automatic clocks and normal cache synchronization.
Only the successful priming length changes:

| Priming length | Three-byte expected output | Actual output | First two suffix-guard bytes | Word at `dst + 1` |
| ---: | --- | --- | --- | --- |
| 6 | `8f 44 3b` | `a5 06 00` | `80 00` | `0x00800006` |
| 10 | `8f 44 3b` | `a5 0a 00` | `80 00` | `0x0080000a` |

`dst + 1` is word-aligned in these cases. Bit 23 is `PKTE_LEN.PEDONE`;
the lower bits encode length 6 or 10. The current request has length 3.
The output therefore tracks the preceding result word, with unchanged
source data and real writes outside the destination payload. This is strong
evidence of stale descriptor-writeback data reaching the packet output.
It does not distinguish a stale write-data latch/packer from incorrect
DMA address/data sequencing, nor identify the bus strobes used. No source
change or workaround is inferred from this capture.

With the normal driver and any earlier test instance unloaded, reproduce
with the unchanged release-10 module. Each invocation is expected to finish
its first configuration and fail the three-byte preflight:

```sh
for prime in 6 10; do
    if insmod eip93_hw_test mode=null lengths="$prime,3" \
        dst_offset=3 depths=1 input_threshold=8 \
        duration_ms=1 warmup_ms=0 runs=1; then
        rmmod eip93_hw_test || exit 1
    fi
done
```

The next comparison uses AES-128 CTR with the same priming sequence and
DMA settings; its outcome has not yet been measured:

```sh
insmod eip93_hw_test mode=ctr direction=encrypt key_bits=128 lengths=6,3 \
    dst_offset=3 depths=1 input_threshold=8 \
    duration_ms=1 warmup_ms=0 runs=1
```

On failure, retain the `src`/`dst payload mismatch`, `payload window`,
`guards` and `Data/guard mismatch` lines. `-74` is a software data mismatch;
a detected guard overwrite takes precedence and returns `-75`. Sources
are verified first, so a `dst` failure means source verification passed.
That checks memory contents, not the bytes actually read by the hardware.
An actual byte still equal to the poison value `a5` is consistent with an
unwritten byte, but does not exclude the same value being written.

## Payload coherency experiment

`dma_skip_cpu_sync=1` passes `DMA_ATTR_SKIP_CPU_SYNC` to payload source and
destination map/unmap calls. It also skips explicit CPU/device sync calls
around verification and output poisoning, so those calls cannot hide a
missing coherency mechanism. The option defaults to zero, is read-only
while loaded, and is printed in the run header.

Compare otherwise identical runs, unloading the test between them:

```sh
insmod eip93_hw_test lengths=1420,1428,1432,1440,1448,1452,1460
rmmod eip93_hw_test
insmod eip93_hw_test lengths=1420,1428,1432,1440,1448,1452,1460 dma_skip_cpu_sync=1
```

Data, source, guard and completion checks still run. Ring, SA and state
records retain their coherent allocations, and DMA ordering barriers remain
in place. A passing run provides evidence for this workload and platform's
DMA mapping behavior; it does not establish hardware coherency generally.
For example, a DMA backend can provide an uncached mapping or perform
additional work beyond the cache operations controlled by this attribute.
Mapping and explicit synchronization already occur outside timed intervals,
so the throughput difference is not a direct measurement of their cost.

On AN7581, the release-6 MSS sweep passes all 294 timed rows and 5,391,906
requests with normal synchronization. With `dma_skip_cpu_sync=1`, the
first NULL-copy preflight fails its data comparison at 1420 bytes and
depth 1, before any timed rows. Keep normal synchronization on this setup;
the capture does not distinguish stale DMA input from stale CPU-visible
output.

## Result-descriptor batching

`result_batch` is the desired number of completed descriptors per interrupt,
from 1 through 128. It programs `PKTE_RING_THRESH.RDRTHRSH` to
`result_batch - 1`: the hardware interrupts when the result count **exceeds**
that field. The command-descriptor threshold stays zero and its interrupt
remains masked.

With `result_batch=1`, the register is zero and the hardware timeout is
disabled, retaining the earlier behavior. With a larger batch, the test
also sets `TOEN` and programs `RDTO` from `result_timeout` (default 5,
range 0..15). The hardware timeout is approximately `2^(N+10)` internal
clock cycles: 32,768 cycles for N=5 or 262,144 for N=8. Its duration in
microseconds depends on the engine clock. The separate `timeout_ms` option
is the software watchdog for a stalled test, not the batching timer.

With `irq_poll=0`, batched runs wait for a result interrupt before draining
ready descriptors; checking ownership cannot bypass that wait. After
the wakeup, the test consumes all currently ready results, decrements the
result count once and acknowledges the interrupt. Actual batches can be
larger than the threshold, or smaller when the timer fires. The timer
also services preflight, queues with `depth < result_batch`, and a final
partial batch after submissions stop. Those small queues can therefore
be dominated by timeout latency.

The verified register value and selected batch/timeout policy are printed
in each run's header. For example, `result_batch=32 result_timeout=8`
programs `PKTE_RING_THRESH=0xa01f0000` at offset `0x08c`.

With the normal driver and any earlier test instance unloaded, compare:

```sh
for batch in 1 8 32; do
    insmod eip93_hw_test lengths=1440,16400,65536 depths=32 \
        result_batch="$batch" result_timeout=8 || break
    rmmod eip93_hw_test || break
done
```

This keeps burst 6, thresholds 0/64, normal DMA synchronization and all
three AES key sizes. Compare throughput and `interrupts / requests` in
the existing CSV; fewer interrupts do not necessarily improve throughput.
The normal driver still has `RDRTHRSH=0`. Its enabled timeout alone does
not delay notification of the first result or configure count batching.

## Mixed IRQ and polling mode

`irq_poll=1` uses a NAPI-like policy in the existing submission thread;
it does not register a network NAPI instance or change the crypto driver.
The first result interrupt masks further result interrupts and wakes the
thread. It drains at most `poll_budget` completions (default 64, range
1..128), acknowledges them together, yields when another task needs the
CPU and replenishes the queue. Productive rounds keep polling with result
interrupts masked, including after exhausting the budget.

When a round finds no ready result, the thread rearms result interrupts
and checks descriptor ownership again. If a completion arrived during
rearming, it masks result interrupts and resumes polling. Otherwise it
sleeps until an interrupt; there is no timed busy-poll window. Fatal
interrupts remain enabled. The existing watchdog, status/cookie checks,
final drain and DMA cleanup also apply to this mode.

`result_batch` and `result_timeout` still control notification when the
thread is sleeping, including the first interrupt and partial batches.
Once polling is active, ready descriptors can be consumed without waiting
for the count threshold. `poll_budget` is a software limit per round,
independent of that hardware interrupt threshold and queue depth.

With the normal driver and any earlier test instance unloaded, compare
both modes at identical depths, lengths and hardware settings:

```sh
for batch in 1 8 32; do
    for poll in 0 1; do
        insmod eip93_hw_test lengths=16,1440,16384 depths=128 \
            result_batch="$batch" result_timeout=8 \
            irq_poll="$poll" poll_budget=64 || exit 1
        rmmod eip93_hw_test || exit 1
    done
done
```

This retains normal DMA synchronization, automatic clocks, burst 6,
thresholds 0/64, all AES keys and both directions. Compare throughput and
interrupts per request. Polling counters show whether the test stays in
polling long enough to reduce interrupt traffic; they do not measure CPU
time. A separate CPU/perf comparison is needed to establish CPU savings.

## Operations and measurements

Each request is one basic-mode descriptor. NULL-copy selects NULL cipher
and NULL hash with payload copying enabled. It selects zero padding,
clears stream padding and uses `PADCTLSTAT=1`; this avoids the NULL mode's
default padding boundary. No ESP or TLS protocol operation is requested.

CTR selects AES with a NULL hash and a separate, immutable initial counter
for each slot. `SAVE_IV` is clear so each descriptor reloads that counter.
The counter starts at one and cannot wrap its low 32 bits within the tested
lengths. Encryption and decryption are verified against software-generated
answers. The test intentionally does not measure IV writeback or CTR
counter-wrap handling.

Default source and destination starts are aligned to the greater of 128 bytes
and the architecture's DMA cache alignment. `src_offset` and `dst_offset`
shift their respective starts independently. Each slot owns separate buffers,
SA and state records. Sources are mapped `DMA_TO_DEVICE`; destinations are
mapped `DMA_BIDIRECTIONAL` so the initialized guards are preserved. There
are no driver bounce buffers, SG fragments, overreads or output copyback.
The DMA layer may still use SWIOTLB when required by the platform. Normal
cache synchronization is enabled unless `dma_skip_cpu_sync=1` is selected.

Mappings persist for the configuration's trials. Allocation, software AES,
mapping, output poisoning, cache synchronization and comparison are outside
timing. The CPU does not touch device-owned payload memory during a trial.
Command/result rings and SA/state records use coherent DMA allocations.

By default, the result ring threshold is zero and its timer is disabled.
The submitting thread reaps available results and sleeps on an interrupt
when none are ready. With batching enabled and `irq_poll=0` it waits for
the threshold or timeout interrupt before each drain. The optional mixed
mode keeps polling across productive rounds as described above. Neither
mode busy-waits on an empty result ring. One
submission thread keeps up to `depth` descriptors outstanding, batching doorbells and result
acknowledgements. Timing starts before the first submission and includes
the final drain, even when it extends beyond `duration_ms`.

`PKTE_CLK_CTL=0` lets autonomous ring mode request clocks dynamically, as
specified in the hardware reference. Release 4 uses this setting during
measurement, but forces PE/AES clocks on for register snapshots, setup,
reset and restoration. Direct reads of gated registers can return zero;
the ability to wake for ring work does not ensure idle register visibility.
The optional
`force_clocks=1` keeps those two clocks enabled (`PKTE_CLK_CTL=0x5`) during
the sweep, matching release 1. Its performance effect needs a device A/B
test; automatic gating does not mean the engine operates without a clock.

CSV lines begin with `row,` and follow this header:

```text
kind,mode,direction,key_bits,length,depth,run,requests,elapsed_ns,mbps,ns_per_request,interrupts,polls,budget_hits,irq_rearms,rearm_ready,dst_offset,src_offset
```

`mbps` is decimal megabits of payload per second; divide by 1000 for Gbps.
Payload bytes equal `requests * length`. The hardware reads and writes
that payload, so aggregate data-memory traffic is approximately twice the
reported payload rate, plus descriptors and context accesses.
`ns_per_request` is elapsed time divided by completions, not individual
request latency at depth greater than one. `interrupts` counts handled
interrupts during the trial; ready results can also be reaped before an
interrupt is needed.

Release 8 appends four counters, preserving the earlier column order:

- `polls`: polling rounds, including empty rounds, in mixed mode.
- `budget_hits`: rounds consuming exactly `poll_budget` results. This
  does not imply another result was already ready.
- `irq_rearms`: attempts to enable result interrupts before a wait,
  counted in both modes, including attempts canceled by the recheck.
- `rearm_ready`: mixed-mode rearms that found a ready descriptor on the
  second check and returned to polling instead of sleeping.

With `irq_poll=0`, `polls`, `budget_hits` and `rearm_ready` are zero.
Release 9 appends `dst_offset`, the requested destination byte offset.
Release 11 appends `src_offset`, preserving the preceding column order.

These results include descriptor preparation, barriers, MMIO, IRQ handling,
scheduling and waits. They are not pure AES execution time or peak bus
bandwidth. Comparing with the Crypto API benchmark also changes mapping,
IV writeback and result-processing behavior; the entire difference
cannot be attributed to one driver function.

## Correctness and cleanup

Before timing, every slot completes one operation and is verified. After
warmup and every trial, the module compares source contents and the final
destination contents with software references and checks surrounding guards.
Outputs are poisoned before each trial so previous correct output cannot
hide a missing write for the whole trial. Every completion checks owner
bits, hardware error/padding status, output length and its ordered cookie.
Intermediate timed outputs are overwritten and are not each compared; use
the existing correctness matrix for broader cipher/SG coverage.

Both ring-owner fields must indicate completion before results are read,
with DMA barriers at submission and completion. Errors and timeouts reset
the PE and ring state machines before DMA storage is released. Successful
runs also stop the engine before freeing rings. The module restores the
previous DMA, buffer threshold, endian, IRQ-mode and clock settings but
leaves autonomous ring mode disabled and ring addresses cleared. Both resets
are explicitly released in idle direct-host mode after reset has cleared
command ownership. Restoration verifies `PE_CONFIG=0`, zero ring addresses
and empty command/result counts. Restored values are checked while
PE/AES clocks remain enabled; the saved clock setting is then restored and
checked independently, including when another restoration check fails.
It never restarts the previous driver's rings.

## Parameters

| Parameter | Default | Accepted values |
| --- | --- | --- |
| `mode` | `both` | `null`, `ctr`, `both` |
| `direction` | `both` | `encrypt`, `decrypt`, `both`; CTR only |
| `key_bits` | 128,192,256 | Comma-separated unique sizes from 128,192,256; CTR only |
| `lengths` | 16,64,256,1024,4096,16384,16400,65536 | Up to 32 exact byte lengths, each 1..65536 |
| `src_offset` | 0 | Source offset in bytes from its aligned start, 0..PAGE_SIZE-1 |
| `dst_offset` | 0 | Destination offset in bytes from its aligned start, 0..PAGE_SIZE-1; independent of src_offset |
| `depths` | 1,32 | Up to 16 entries, each 1..128 |
| `result_batch` | 1 | Desired results per interrupt, 1..128; writes threshold = batch - 1 |
| `result_timeout` | 5 | Raw RDTO exponent N, 0..15; enabled only when result_batch > 1 |
| `irq_poll` | 0 | Mask result IRQs while polling/refilling; rearm when no completion is ready |
| `poll_budget` | 64 | Maximum completions per polling round, 1..128 |
| `duration_ms` | 250 | 1..60000 |
| `warmup_ms` | 50 | 0..60000 |
| `runs` | 3 | 1..20 |
| `seed` | 1708171853 | Reproducible payload/key/IV seed |
| `timeout_ms` | 10000 | 1..60000, maximum time without completion |
| `dma_maxburst` | 6 | Raw MXBRSTSZ field, 0..15; not a byte count |
| `input_threshold` | 0 | Raw input threshold field, 0..255 |
| `output_threshold` | 64 | Raw output threshold field, 0..255 |
| `force_clocks` | 0 | 0: automatic clock gating; 1: force PE/AES clocks on |
| `dma_skip_cpu_sync` | 0 | 1: skip payload map/unmap and explicit cache synchronization for coherency testing |

The default sweep produces 336 timed rows: eight lengths, two depths and
seven operations (NULL-copy, plus CTR encrypt/decrypt at each of three
key sizes), with three trials each. Its requested warmup/timing windows
total about 90 seconds; setup, verification, final draining and slow
configurations add wall time.
Using `key_bits=128` restores the original 144-row sweep and roughly
38 seconds of requested warmup/timing windows.

## First device capture

The first AN7581 capture, with revision `0x0301a25d`, passes all 144 rows,
completing 5,460,128 timed requests and transferring 3,979,140,544 payload
bytes. It covers NULL-copy and AES-128 CTR in both directions, eight lengths
and depths 1/32. Source/output/guard checks at preflight and trial boundaries
pass; intermediate timed outputs are not individually compared.

The recorded configuration is burst 6 and thresholds `0x00800080`, meaning
input/output 128/128. It is **not a 0/64-threshold baseline**. Release 1 wrote
tuning settings with PE reset still asserted and did not enforce readbacks.
It also selected the generic endian offset `0x1cc` for PonWrt's generic-only
engine node, producing `endian=0`. AN7581 uses offset `0x1d0`; that actual
register was not captured. Release 2 also checks the root SoC compatible
when selecting the endian offset.

Median payload throughput in Gbps across three trials, at depth 32:

| Bytes | NULL-copy | CTR encrypt | CTR decrypt |
| ---: | ---: | ---: | ---: |
| 16 | 0.106052 | 0.083104 | 0.083091 |
| 64 | 0.396147 | 0.278242 | 0.278794 |
| 256 | 1.003098 | 0.682866 | 0.681853 |
| 1024 | 1.502252 | 1.054541 | 1.054606 |
| 4096 | 1.676229 | 1.208820 | 1.208735 |
| 16384 | 1.708376 | 1.259284 | 1.259608 |
| 16400 | 1.696828 | 1.259697 | 1.258185 |
| 65536 | 1.713946 | 1.272824 | 1.272960 |

Bulk CTR encryption and decryption have essentially equal throughput here.
At depth 1, small operations take about 8.7 microseconds, so pipeline depth
matters most at short lengths. These measurements include submission,
interrupt and scheduling costs; the NULL/CTR difference is not a direct
measurement of AES core execution time.

Large transfers show almost two interrupts per request. Release 1 cleared
the result interrupt before decrementing the result count, permitting the
level condition to relatch. Release 2 clears it again after consuming the
results. A model regression reproduces two interrupts per request without
that acknowledgement and one with it. The later capture below verifies
the interrupt improvement on the device with forced clocks.

## Release 2/3 device captures and clock-ordering fix

With automatic gating, release 2 aborts setup with the threshold readback
at `0x10c` returning zero instead of `0x00400000`. With `force_clocks=1`,
it verifies burst 6, thresholds 0/64 and endian `0x00e400e4` at offset
`0x1d0`, then completes all 144 AES-128/NULL measurement rows and 5,716,652
timed requests without data/guard errors. In both runs, restoration reports
`0x100` as zero instead of the requested ring-reset value `0x2`.

The setup and restoration arrays wrote the clock setting before reading
back the other registers. Thus automatic gating could hide the setup
values, and restoring the saved zero clock setting could hide the idle
configuration. A zero readback does not establish that the earlier write
failed. The initial settings snapshot also needed clocks enabled to avoid
saving a gated read as the old value. Release 4 fixes these three points;
automatic gating remains the measurement default.

The forced-clock rows remain useful measurements even though insertion
fails at cleanup. At 64 KiB and depth 32, median payload throughput is
approximately 1.710 Gbps for NULL-copy and 1.316 Gbps for CTR in either
direction, with exactly one handled interrupt per request in those rows.
This confirms that the duplicate-interrupt issue is resolved in this
capture. It does not validate automatic gating on the device, nor isolate
the performance effect of the threshold and interrupt changes.

A subsequent release-3 capture selects `key_bits=192,256` with the same
verified burst, thresholds, endian and forced-clock settings. All 240
measurement rows complete, covering 8,365,697 timed requests, without
data/guard errors; insertion then fails at the same restoration check.
At 64 KiB and depth 32, CTR reaches approximately 1.057 Gbps encrypt /
1.059 Gbps decrypt for AES-192 and 0.958 Gbps in either direction for
AES-256. Each of those rows has exactly one interrupt per request. This
validates the larger-key measurement paths on the device. The subsequent
automatic-clock capture is described below.

## Release 4 automatic-clock capture and restore check

Release 4 verifies the intended configuration with `clocks=0x0` and completes
28 rows and 302,730 timed requests. This covers NULL-copy and all three AES
key sizes in both directions, at lengths 16/65536 and depths 1/32. No data
or guard check fails. Restoration still reports `PE_CONFIG=0` instead of
the requested `0x2`, even though the checks now run with clocks forced on.
Thus the earlier clock-ordering explanation did not account for this
remaining failure.

The ADSP-SC58x reference, table 44-37 (page 44-60), describes `RSTRING` as
a reset that software asserts with one and releases with zero; it does
not describe it as self-clearing. The AN7581 observation establishes a
zero readback, not why it reads zero. Release 5 avoids relying on a
persistent asserted-bit readback: it retains the PE/ring reset sequence,
then explicitly writes `PE_CONFIG=0` and checks the idle configuration,
cleared ring addresses and empty descriptor counts before restoring clocks.
Releasing reset in direct-host mode does not enable autonomous ring fetching.

## Release 5 successful device validation

The AN7581 short sweep completes with
`PASS rows=28 requests=291731; engine stopped`, including restoration.
It checks all three AES sizes and both CTR directions alongside NULL-copy,
at lengths 16/65536 and depths 1/32, using one 50 ms trial without warmup.

The subsequent full default sweep also passes:
`PASS rows=336 requests=11696104; engine stopped`. It covers all eight
lengths, depths 1/32, AES-128/192/256 encryption and decryption, and NULL-copy,
with 50 ms warmups and three 250 ms trials per configuration. Both captures
verify burst 6, thresholds 0/64, endian `0x00e400e4` at offset `0x1d0` and
automatic clocks (`clocks=0x0`). There are no reported completion,
preflight/final data, guard or restoration errors.

The full CSV matrix, rates and request totals are checked independently.
Timed payload totals 7,744,434,496 bytes (7.213 GiB). Timed intervals sum
to 84.488 seconds including final draining; total capture time is 94.287
seconds including setup, warmups, verification and restoration.

Median payload throughput in Gbps at depth 32 (CTR encryption shown):

The 1440-byte row is from the release-6 MSS sweep, with matching burst 6,
thresholds 0/64, automatic clocks and normal DMA synchronization. All other
rows are from the release-5 default sweep. Each value is the median of
three trials, calculated from exact request counts and elapsed times.

| Bytes | NULL-copy | AES-128 CTR | AES-192 CTR | AES-256 CTR |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 0.112873 | 0.087596 | 0.086329 | 0.083897 |
| 64 | 0.399127 | 0.278458 | 0.266369 | 0.263773 |
| 256 | 1.016327 | 0.703456 | 0.639537 | 0.578719 |
| 1024 | 1.480190 | 1.055650 | 0.938525 | 0.830064 |
| 1440 | 1.567095 | 1.142105 | 0.989664 | 0.867279 |
| 4096 | 1.675800 | 1.251068 | 1.056109 | 0.924212 |
| 16384 | 1.708187 | 1.299221 | 1.069006 | 0.951402 |
| 16400 | 1.700225 | 1.300475 | 1.062057 | 0.951130 |
| 65536 | 1.709602 | 1.316053 | 1.058550 | 0.958264 |

At depth 32, decrypt medians differ from encrypt medians by at most 0.41%
across the release-5 size/key combinations. At 64 KiB, decrypt rates are 1.316050,
1.057079 and 0.958273 Gbps for AES-128/192/256. All 64 KiB/depth-32 trials
have exactly one handled interrupt per request.

For AES-128 encryption, increasing depth from 1 to 32 raises throughput
5.34 times at 16 bytes, 12.5% at 16,400 bytes and 3.74% at 64 KiB. The
16,400-byte point improves from 1.156271 to 1.300475 Gbps. This shows the
largest queue-depth benefit at short lengths, with bulk throughput nearly
flat by 16 KiB. These figures include ring submission, IRQ and scheduling
costs; they are not pure cipher-core measurements or a kTLS throughput limit.

The next comparison is the [Crypto API CTR benchmark](../eip93-test/README.ctr-bench.md)
with the normal driver loaded, matching burst 6 and thresholds 0/64 and
running each key size separately. It adds driver processing and per-request
DMA mapping, IV handling and driver completion handling to this baseline.
The source remains aligned and contiguous, so it does not measure the fragmented
CTR/GHASH work of kTLS.

## Release 7 device result-batching comparison

The AN7581 depth-32/batch-32 sweep passes 168 measurement rows and
6,336,720 timed requests. It covers the eight default lengths, NULL-copy
and all AES keys/directions with `result_timeout=8`. At 16 bytes, AES-128
encryption falls from the earlier immediate-notification baseline of
0.087596 Gbps to 0.052822 Gbps; the queue empties before it can be refilled.
At 16 KiB, throughput remains approximately 1.297 Gbps and the timeout
limits the effective batch to about five requests per interrupt.

The following depth-128 comparison uses three trials per configuration,
burst 6, thresholds 0/64, normal DMA synchronization, automatic clocks,
and `result_timeout=8` when batching. All 189 rows and 12,506,517 timed
requests pass, including cleanup. Median AES-128 CTR encryption rates:

| Length | Batch 1 Gbps | Batch 8 Gbps | Batch 32 Gbps | Batch 32 IRQs/request reduction vs batch 1 |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 0.090939 | 0.084045 | 0.082527 | 96.75% |
| 1440 | 1.141675 | 1.135614 | 1.133860 | 97.13% |
| 16384 | 1.304489 | 1.296759 | 1.297258 | 80.27% |

At 1440 bytes, batch 32 averages 35.1 requests per interrupt, versus 1.00
at batch 1, with 0.68% lower throughput. At 16 KiB, batches 8 and 32 both
average about 5.06 requests per interrupt because of the timer. A deeper
queue reduces the short-message throughput penalty but does not eliminate
it. IRQ counts do not establish CPU savings. These captures use the
release-7 completion policy; mixed IRQ/polling hardware results are pending.

## Local validation

Host GCC builds the standalone module against the prepared Linux 6.18
test kernel with `W=1`; strict checkpatch passes. A local QEMU TCG fixture
compiles this same source with modeled MMIO, DMA completion and IRQs,
using `ctr(aes-generic)` as its emulated cipher. Release 2 passed fourteen
sweeps covering 213 timed rows, all AES key sizes, lengths through 65536, depths
1/3/32/128, ring wraparound, immediate completion and an interrupt arriving
before both result owner fields are ready. CSV rates and completion totals
are independently checked.

The fixture now models lost configuration writes while PE reset is asserted
and a latched, level-triggered result interrupt. Tests cover the generic
register map, the Airoha device compatible, the older AN/EN7581 and AN/EN7583
root-compatible paths, and automatic/forced clock settings. In the isolated
IRQ regression, the old acknowledgement sequence gives 88 interrupts for
44 requests; the fixed sequence gives 48 interrupts for 48 requests. These
counts demonstrate the model's behavior, not hardware interrupt timing.

Thirty-nine expected fixture rejections cover invalid parameters, absent
AES capability, wrong output, guard/source corruption, bad cookies/lengths,
descriptor errors, fatal IRQs, a stalled ring and eleven setup readback
mismatches. Cleanup assertions check
that DMA is stopped before memory is released and settings are restored.
The actual module also rejects loading without an EIP93 device in QEMU.

Release 3 passes twelve focused sweeps covering 414 timed rows and 61,123
requests. They exercise the default three-key sweep, single sizes, subsets
in the requested order, encrypt/decrypt selection, and a single NULL-copy
baseline per length/depth even without hardware AES support. CSV order,
rates, request totals and the exported key list are checked independently.
Fifteen model rejections cover invalid/duplicate/empty key lists, missing
key-size capabilities, and a failure on AES-192 after successful AES-128
trials. The actual module also rejects no-device probing. Cleanup assertions
pass for both successful and failed loads. These are harness/model checks,
not measurements of the physical accelerator.

Release 4 adds modeled clock-gated register access. The old source
reproduces the automatic-clock setup error and the forced-clock restoration
error. The fix passes sixteen sweeps with 432 timed rows and 42,543 requests,
covering both clock modes, saved clock values 0/1/5/31, all three AES sizes,
all endian-selection paths, and immediate/early completion. Thirty model
rejections cover eight processing faults, eleven setup and ten restoration
readback faults, and IRQ acquisition failure; the actual module also rejects
no-device probing. Assertions verify zero gated configuration accesses,
restoration of the saved tuning/clock values and DMA quiescence before
freeing memory. The earlier fixtures did not model clock-gated reads; these
release-4 checks still assumed the ring-reset bit read back as written.

Release 5 tests both retained and zero ring-reset readbacks. The release-4
control reproduces the restoration error with automatic and forced clocks.
Eight successful sweeps cover 135 timed rows and 19,637 requests, all three
AES sizes, both clock modes and immediate/early completion. Thirty-two
model rejections cover processing faults with either reset readback, setup
and restoration readback mismatches, nonempty command/result counts, an
unexpected operating mode and IRQ acquisition failure. The actual module
also rejects no-device probing. Assertions verify idle direct-host mode,
empty rings, saved settings and DMA quiescence at cleanup. This tests the
software sequence against both modeled readback behaviors; it does not
establish why the AN7581 reset bit reads zero.

Release 5's short and full default device sweeps pass as recorded above.
Release 6 builds with host GCC and `W=1` and passes strict checkpatch.
Six QEMU MMIO/DMA/IRQ-model sweeps pass 512 timed rows and 21,788 requests,
covering the seven MSS sizes, lengths 1..17/31/32/33/65535/65536, all AES
key sizes, both directions and NULL-copy. Normal and skipped cache sync
are tested, including early interrupts and immediate completion.
Independent checks verify the CSV matrix, exact byte rates and request
totals. Reference-buffer guards cover every length 1..64 for all key sizes.

Twenty-one expected model rejections cover invalid parameters, data/guard
corruption, invalid result cookies/lengths, fatal IRQs, a stalled ring and
failure at each of the first three DMA mappings. The actual module also
rejects probing without a device. All 42 mapping audits verify matching
map/unmap attributes and DMA quiescence before cleanup. The 4,038 mappings
using `DMA_ATTR_SKIP_CPU_SYNC` have zero explicit CPU/device sync calls;
normal runs retain synchronization. These are harness/model checks, not
device coherency results. The subsequent device MSS sweep passes with
normal synchronization and fails preflight with synchronization skipped,
as recorded above.

Release 7 builds with host GCC and `W=1` without warnings; both source files
pass strict checkpatch. Thirteen QEMU model sweeps pass 294 timed rows and
21,449 requests. They cover batches 1/2/8/32/128, timeout exponents 0/5/8/15,
queue depths below/equal/above the requested batch, all AES key sizes,
partial-block/MSS/bulk lengths, automatic/forced clocks and immediate/early
completion. CSV matrices, rates, request totals and ring-threshold encodings
are checked independently.

The delayed-completion control records 80 interrupts for 80 timed requests
with batch 1, 10 for 80 with batch 8, and 2 for 64 with batch 32. Timeout
interrupts service queues that cannot reach the threshold and final partial
batches. Removing the modeled timer makes a depth-1/batch-32 preflight
time out despite a ready descriptor; a control retaining the old polling
behavior completes without an interrupt, demonstrating why the wait path
must also respect batching.

Sixteen expected model rejections cover malformed parameters, processing
errors, failed DMA mappings, a ring-threshold readback mismatch and the
missing timer. The actual module also rejects no-device probing. Thirty-nine
cleanup audits verify mapping release, idle rings, restored settings and
DMA quiescence. These checks validate software behavior against modeled
threshold/timeout semantics. Subsequent physical EIP93 batching captures
are recorded above.

Release 8 builds with host GCC and `W=1` without warnings and passes strict
checkpatch. Twenty-six QEMU model sweeps pass 947 timed rows and 119,460
requests, including all release-7 batching controls. Mixed-mode tests cover
budgets 1/2/7/64/128, depths 1/3/32/128, all AES keys, partial/MSS/bulk
lengths, asynchronous and immediate completion, and early IRQs. Assertions
check that result interrupts are masked during each polling acknowledgement
and that no acknowledgement exceeds the configured budget.

With continuously ready model results, a batch-32/depth-32 control handles
244 interrupts for 7,808 timed requests; mixed mode handles one interrupt
for 4,480 requests across 560 budget-8 rounds. A second mixed-mode trial
also uses one interrupt across 475 rounds. These demonstrate that polling
continues through budget exhaustion and refill; modeled elapsed times are
not accelerator performance measurements.

Completions injected during IRQ rearming exercise 4,730 second-check hits
in timed rows, including below-threshold completions at depth 3/batch 8.
Thirty expected model rejections plus actual no-device probing cover bad
parameters, data/guard/source errors, cookies, lengths, descriptor errors,
fatal IRQs, a stalled ring, failed mappings and a missing hardware timer.
All 76 cleanup audits verify stopped DMA, released mappings and restored
settings. CSV matrices, rates, counts, policy logs and register encodings
are checked independently. Mixed IRQ/polling performance on the device
remains to be measured.

No OpenWrt target build was run locally.

Release 9 builds with host GCC and `W=1` without warnings and passes strict
checkpatch. Twelve QEMU model sweeps pass 365 timed rows and 30,377 requests.
They cover destination offsets 0/1/2/3/4/127/128/129/4095, lengths through
65536 bytes, partial word/block tails, all AES keys, NULL-copy, both CTR
directions, depths 1/3/32, interrupt batching and mixed polling. The model
checks actual descriptor address offsets and aligned mapping boundaries;
CSV offsets, matrices, byte rates and request counts are checked separately.

Fourteen expected model rejections cover invalid offsets, data/source
corruption, writes immediately before/after the shifted destination, and
failed DMA mappings. The actual module rejects probing without a device.
All 34 cleanup audits verify stopped DMA, released mappings and restored
settings. These validate the test software; unaligned destination DMA
findings on the device, including failing lengths, are recorded above.

Release 10 builds with host GCC and `W=1` without warnings and passes
strict checkpatch. Two QEMU model sweeps pass 112 timed rows and 11,830
requests, covering offsets 1/2, lengths 1/2/256/1024, depths 1/32 and all
operations/key sizes. Seven injected failures verify source/destination
labels, first/last offsets, byte counts, bounded expected/actual dumps and
prefix/suffix guard reporting. They include a tail failure in slot 7 of
a depth-32 preflight. All 16 cleanup audits pass; the actual module still
rejects probing without a device. These check diagnostic accuracy and do
not reproduce or fix the physical device's corruption. No OpenWrt target
build was run.

Release 11 adds independent source offsets. Local builds and tests were
not run for this change, as requested; the validation results above apply
to the earlier releases.
