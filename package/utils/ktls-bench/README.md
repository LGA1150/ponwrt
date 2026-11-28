# Standalone kTLS throughput benchmark

`ktls-bench` measures kernel TLS over local loopback TCP. It supports TLS 1.2
and 1.3, RX with `recv` or splice-to-`/dev/null`, TX with `send` or splice,
and multiple simultaneous TLS connections. Each TLS worker has a separate
raw TCP peer thread. Output includes CSV trials, throughput summaries,
thread CPU time and system-wide busy time.

The package bundles the existing
[`tls_rx_bench.c`](https://github.com/LGA1150/linux/blob/fa99e45c5efc2ba7632cb7d1aa938470351f242a/tools/testing/selftests/net/tls_rx_bench.c)
unchanged from the Linux test branch. Its filename and `tls_rx_bench:` error
prefix are retained; the installed executable is `/usr/bin/ktls-bench`.
Only libc, pthreads and Linux UAPI headers are needed to compile it.

## OpenWrt package

Select **Utilities -> ktls-bench** (`CONFIG_PACKAGE_ktls-bench`). Build just
this package with:

```sh
make package/utils/ktls-bench/compile V=s
```

The package selects `libpthread`, `kmod-crypto-gcm` and `kmod-crypto-ccm`.
Supply kernel TLS support separately: select either stock `kmod-tls` or
the out-of-tree [`kmod-ktls`](../../kernel/README.ktls-eip93.md), or use a
kernel with TLS built in. The two module packages conflict, so the benchmark
does not select either one automatically. Load the chosen TLS module before
running if it is not already loaded:

```sh
modprobe tls
ktls-bench -h
```

No OpenSSL, AF_ALG userspace interface, EIP93 driver, remote host or
certificate is required. The tool supplies fixed test keys directly to
kTLS and performs no TLS handshake. It uses whichever kernel crypto backend
the selected AEAD resolves to; it does not change algorithm priorities or
module parameters. Stock and patched kernels can run the same binary.

The source also builds independently with `make -C src`, or a cross-compiler
passed as `make -C src CC=your-target-gcc`.

## Run

The default is TLS 1.3 AES-GCM-128 RX, 4096 records of 16384 bytes, one TLS
worker, one warmup and five measured trials. The 64 MiB of plaintext and
its ciphertext are held in memory; allow approximately 128 MiB plus socket
and thread overhead, or reduce `-n` on smaller devices.

```sh
# Short correctness run with two TLS connections.
ktls-bench -d rx -v 1.3 -j 2 -p 0 -n 257 -w 0 -r 1

# Current TLS 1.3 RX comparison: 64 MiB total across two connections.
ktls-bench -d rx -v 1.3 -j 2 -p 0 -n 4096 -w 1 -r 5

# TLS 1.2 RX splice-to-null.
ktls-bench -d rx -v 1.2 -m splice -j 2 -p 0

# TX send and splice, with both supported GCM key lengths.
ktls-bench -d tx -v 1.3 -m send -c gcm -k 128 -j 2
ktls-bench -d tx -v 1.2 -m splice -c gcm -k 256 -j 2

# AES-CCM with a 128-bit key and 16-byte tag.
ktls-bench -d rx -v 1.3 -c ccm -k 128 -j 2 -p 0
```

`-n` is the total record count per trial, divided across `-j` workers; it
must be at least the worker count. `-b` bounds records per I/O request
(default 64), not the kernel's crypto pipeline depth. `-p` is the RX prefill
per connection; use `-p 0` to include feeding the whole transfer in timing.
`-R` and `-T` pin the receiving and transmitting roles to CPUs. See `-h` for
all options. Supported ciphers are AES-GCM-128, AES-GCM-256 and AES-CCM-128;
AES-192 and CCM-256/CCM-8 are not exposed by the kTLS UAPI.

## Verification and accounting

RX preparation encrypts records through kTLS TX outside the timed trials.
Every recv trial verifies the plaintext after timing. RX splice performs
an untimed recv preflight, then checks byte counts and EOF while discarding
the timed output through a pipe into `/dev/null`; its CSV summary therefore
reports `verified=length`.

TX timing includes sending plaintext and capturing all ciphertext at the
raw peer, including outstanding asynchronous crypto. Each capture is then
decrypted through kTLS RX and checked against the plaintext outside timing.
TX splice includes filling the source pipe. Thus RX preparation and TX
verification exercise the opposite kTLS direction as well. These are
end-to-end kTLS consistency checks, not independent cryptographic vectors.

Throughput counts plaintext bytes. Thread CPU columns exclude asynchronous
crypto workers. `system_busy_s` includes all work on the machine, sampled
from `/proc/stat`; keep the host otherwise idle and compare the same kernel
accounting configuration. Driver counters collected around the whole
command also include preparation, warmups and untimed verification. Keep
that counting window identical when comparing IRQ batching settings.

For driver profiling with the existing helper, pass the installed executable
explicitly:

```sh
eip93-measure-ktls /usr/bin/ktls-bench /tmp/ktls/profile-results
```
