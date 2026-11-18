# Aligned CTR throughput

Test package release 9 adds `eip93_ctr_bench.ko`, separate from the existing
`eip93_test.ko` correctness matrix. It measures `ctr(aes-eip93)` directly
through the kernel Crypto API, without sockets, kTLS or GHASH. Driver
release 37 needs no change for this measurement.
Test package release 10 accepts exact byte lengths, including partial
final CTR blocks and common TCP MSS sizes.

For a baseline that bypasses the crypto driver entirely, the separate
[`eip93_hw_test.ko` module](../eip93-hw-test/README.md) measures aligned
NULL-copy and AES-CTR directly through EIP93's descriptor rings. It requires
unloading the normal driver and claims the device exclusively.

Each outstanding request owns separate source and destination buffers.
Each buffer has one SG entry, a start aligned to at least 128 bytes, and a
DMA extent rounded up to the greater of 128 bytes and the architecture's
DMA cache alignment. The crypto operation still processes exactly the
selected length. The surrounding padding and guards are checked for
unexpected writes. Lengths can be any value from 1 through 65,536 bytes.
The software reference checks exact output and the returned IV, including
the counter increment for a partial final block.

These requests use the driver's single-entry path, including when
`skcipher_tail_align=1`. This is a baseline for operation size and queue
depth; it does not reproduce the fragmented GCM source layout. The
16,400-byte point matches the total CTR length of a full-size TLS record
plus its GCM prefix, but remains a single contiguous input here.

## Device command

Install the updated `kmod-eip93-test` package and keep the EIP93 driver
loaded. Before timing, confirm `aes_sw_fallback=N`,
`skcipher_dma_debug=N` and `skcipher_profile=N` in
`/sys/module/crypto_hw_eip93/parameters/`. Selecting an EIP93 driver by
name does not bypass a fallback inside that driver. In particular, stock
or earlier drivers can send small AES requests to software.

Keep `dma_maxburst=6` and the current input/output thresholds at 0/64 for
the first comparison. On the tested Airoha device the readbacks are:

```sh
devmem 0x1fb70120 32  # expected 0x00180006
devmem 0x1fb7010c 32  # expected 0x00400000
insmod eip93_ctr_bench
dmesg | sed -n 's/^.*eip93_ctr_bench: //p' > /tmp/eip93-ctr-bench.log
rmmod eip93_ctr_bench
```

The module does not write registers or change driver parameters. Save the
readbacks and parameter values with the log. The extraction retains any
earlier benchmark loads still in the kernel log; each load has its own
target/settings header and final status.

The default sweep uses AES-128, both encryption and decryption, lengths
16, 64, 256, 1024, 4096, 16384, 16400 and 65536, and queue depths 1 and 32.
Each length/depth/direction gets a 50 ms warmup followed by three 250 ms
trials. This produces 96 CSV result rows. Setup, verification and draining
can make wall time longer than the roughly 26 seconds of requested
warmup and timed windows.

For a longer RX-direction comparison:

```sh
insmod eip93_ctr_bench direction=decrypt \
    lengths=16,32,64,128,256,512,1024,2048,4096,8192,16384,16400,32768,65536 \
    depths=1,32 duration_ms=1000 runs=5
```

Unload the module before another invocation. `key_bits=192` and
`key_bits=256` select the other AES key sizes.

## Results and accounting

Each `result,...` line follows the emitted CSV header:

```text
kind,direction,key_bits,length,depth,run,requests,bytes,elapsed_ns,mbps,ns_per_request,async,backlogs
```

- `mbps` is decimal megabits per second; divide by 1000 for Gbps.
- `depth` is the maximum number of outstanding requests from one submitting
  thread, with one transform and independent requests/buffers. It is not
  a worker count. A synchronous target cannot overlap operations.
- `ns_per_request` is elapsed time divided by completed requests. At depth
  greater than one it is amortized throughput cost, not request latency.
- `async` counts submissions returning `-EINPROGRESS` or accepted `-EBUSY`;
  `backlogs` counts the latter subset. Backlog promotion callbacks are not
  treated as final completion.
- `bytes` equals `requests * length`, excluding SG padding. Timing starts
  before submission and ends after all submitted requests complete. It
  includes IV resets, submission, scheduling and waits; it is not a pure
  engine execution-time measurement. Final draining is included even if
  it extends beyond `duration_ms`.

Allocation, reproducible plaintext generation with synchronous
`ctr(aes-generic)`, reference encryption, warmup and verification occur
outside each measured interval. Every slot has a distinct reproducible
IV and payload; its original IV is restored for each request. Buffers are
reused throughout a trial.

Every slot is verified before measurement. After warmup and each trial,
the module checks source contents, the final output in each used slot,
the returned CTR IV, padding and guards. It checks every completion status,
but does **not** compare every intermediate timed output: later operations
overwrite those buffers. This throughput test supplements the correctness
matrix and is not a replacement for it.

Allocation or crypto errors fail insertion after outstanding requests have
drained. A stalled request produces a warning after `timeout_ms`, then the
module waits for completion before releasing its buffers. The Crypto API
does not provide cancellation. Successful insertion leaves read-only
`rows` and `requests` counters under
`/sys/module/eip93_ctr_bench/parameters/` until unloading.

## TCP MSS-sized operations

With the normal driver loaded and the settings above, run:

```sh
insmod eip93_ctr_bench lengths=1420,1428,1432,1440,1448,1452,1460 key_bits=128
```

These cover Ethernet and PPPoE, IPv4 and IPv6, with and without the usual
12-byte TCP timestamp options. See the [size table](../eip93-hw-test/README.md#tcp-mss-sized-operations)
for the corresponding MTUs and headers. The requested lengths are exact;
only allocation and DMA extents are rounded for cache ownership. This is
one contiguous SG entry per buffer, not a fragmented TLS record.

The command produces 84 timed rows at depths 1/32 in both directions, with
three trials each. Unload `eip93_ctr_bench` before repeating with
`key_bits=192` or `key_bits=256`. Keep software fallback, DMA debug and
profiling disabled, and burst/threshold settings fixed, for comparison with
the standalone hardware test. Its `dma_skip_cpu_sync` option is separate:
the Crypto API benchmark uses mappings owned by the selected crypto driver.

## Parameters

| Parameter | Default | Accepted values |
| --- | --- | --- |
| `driver` | `ctr(aes-eip93)` | Driver implementing `ctr(aes)` |
| `direction` | `both` | `encrypt`, `decrypt`, `both` |
| `key_bits` | 128 | 128, 192, 256 |
| `lengths` | See default sweep above | Up to 32 exact byte lengths, each 1..65536 |
| `depths` | 1,32 | Up to 16 entries, each 1..128 |
| `duration_ms` | 250 | 1..60000 |
| `warmup_ms` | 50 | 0..60000 |
| `runs` | 3 | 1..20 |
| `seed` | 1708171853 | Reproducible payload/key/IV seed |
| `timeout_ms` | 10000 | 1..60000 |

`driver=ctr(aes-generic)` and `driver=cryptd(ctr(aes-generic))` validate the
harness against synchronous and asynchronous software targets. Those runs
do not measure EIP93. The log always prints the selected driver name.

## Validation

Host GCC builds both test modules with `W=1` without warnings, and the new
source passes strict checkpatch. QEMU TCG passes 14 benchmark loads and
312 timed rows across all AES key sizes, synchronous software, cryptd,
completion/backlog fixtures and the existing EIP93 driver model. CSV byte
counts, rates and request accounting are checked independently. The run
contains 75,650 timed requests, including 19,150 asynchronous submissions
and 601 accepted backlog submissions.

Twenty-four expected failures cover invalid parameters and targets,
submission/completion errors with outstanding work, wrong output, source
modification, guard corruption, wrong returned IV and delayed completion.
All requests are drained before failure returns. Model counters confirm
one descriptor, no software boundary bytes and no source/destination
copies per aligned request, with tail alignment both disabled and enabled.
A replay through the original correctness module also passes.

Release 10 passes 19 further QEMU loads, 516 timed rows and 45,235 timed
requests. All three key sizes cover the seven MSS lengths through generic
software, cryptd and the EIP93 driver model with tail alignment off/on.
Partial-block edges through 65,536 bytes, accepted backlog, synchronous
and immediate asynchronous completion also pass. Six expected failures
cover out-of-range lengths and data/source/guard/IV corruption. CSV
coverage, exact byte counts, rates and request totals are independently
checked; failure paths drain outstanding requests.

These checks validate the harness and its accounting, not EIP93 hardware
throughput. No OpenWrt target build was run locally.
