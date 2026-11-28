# Verified TLS proxy benchmark

`tls-proxy-bench` runs a TLS client and a plaintext TCP backend together on a
Linux PC. The TLS-terminating proxy is the device under test:

```text
PC TLS client <== TLS ==> device: HAProxy <== plaintext TCP ==> PC backend
```

One command owns both endpoints, matches forwarded connections with a random
trial token, and checks every received payload byte against an independent
reference stream. It supports TLS 1.2 and 1.3, multiple simultaneous connections,
and three directions relative to the proxy:

| Direction | Payload path | Proxy operation |
| --- | --- | --- |
| `rx` | TLS client to plaintext backend | Decrypt and forward |
| `tx` | Plaintext backend to TLS client | Encrypt and forward |
| `bidir` | Both flows concurrently on every connection | Encrypt and decrypt |

Traffic streams continuously. There are no per-block acknowledgements or echo
round trips. Bidirectional mode generates different data in each direction;
the backend does not echo the client's bytes.

## Build on the PC

Install a C compiler, OpenSSL development headers/libraries (1.1.1 or newer),
and make, then run:

```sh
make -C package/utils/tls-proxy-bench/src
```

The source also builds by itself:

```sh
cc -O2 -Wall -Wextra -pthread tls_proxy_bench.c \
    -o tls-proxy-bench -lssl -lcrypto
```

The OpenWrt package is optional. Neither the PC program nor the package depends
on the EIP93 driver or the kTLS module. The proxy chooses its own crypto path.

## HAProxy setup

For example, let the PC be `192.0.2.10` and the device be `192.0.2.20`. Substitute
addresses on your LAN. Configure HAProxy 3.3 or newer on the device:

```haproxy
global
    expose-experimental-directives

listen tls_benchmark
    mode tcp
    bind :443 ssl crt /etc/haproxy/server.pem ktls on
    timeout connect 10s
    timeout client 2m
    timeout server 2m
    option splice-request
    option splice-response
    server pc 192.0.2.10:5001
```

Use a current HAProxy point release with `USE_KTLS`, `USE_LINUX_SPLICE` and a
compatible TLS library. A successful benchmark proves delivery and payload
integrity, not that HAProxy actually enabled kTLS or splicing. Check its build
features, runtime counters/tracing and kernel TLS counters separately. Capture
traces in a separate run to avoid perturbing throughput measurements.

The backend listener is plaintext and uses this tool's protocol; do not point
HAProxy at an iperf or echo server. Leave PROXY protocol and application-protocol
health checks disabled for this listener. Empty TCP connect-only health checks
are tolerated. The PC firewall must allow the device to connect to port 5001.

## sing-box setup

The [sing-box recipe](sing-box/README.md) uses a TLS 1.3 Trojan fallback to
forward this benchmark's stream to the PC backend. It includes two patches
for sing-box v1.14.3: enabling server-side RX splice and draining buffered TLS
plaintext before handing reads to splice. The patches are copied into the
external packages feed; installing `tls-proxy-bench` does not apply them.

## Run

The tool opens its listener before connecting to HAProxy. No separate backend
process is required. With a trusted lab CA and a certificate for `proxy.test`:

```sh
# RX: the device decrypts; two connections, 256 MiB per connection.
./tls-proxy-bench --connect 192.0.2.20:443 --listen 192.0.2.10:5001 \
    --ca lab-ca.pem --server-name proxy.test \
    -d rx -v 1.2 -j 2 -n 256M -w 1 -r 5

# TX: the device encrypts.
./tls-proxy-bench --connect 192.0.2.20:443 --listen 192.0.2.10:5001 \
    --ca lab-ca.pem --server-name proxy.test \
    -d tx -v 1.3 -j 2 -n 256M -w 1 -r 5

# Both directions simultaneously on each connection.
./tls-proxy-bench --connect 192.0.2.20:443 --listen 192.0.2.10:5001 \
    --ca lab-ca.pem --server-name proxy.test \
    -d bidir -v 1.3 -j 2 -n 256M -w 1 -r 5
```

For an isolated lab with an untrusted certificate, replace the `--ca` option
with the explicit `--insecure` option. This disables certificate verification;
payload verification remains mandatory. Without either option the tool uses
the system CA store. `--server-name` sets both SNI and the identity to verify;
otherwise the connection hostname/IP is verified.

Both address arguments accept bracketed IPv6 addresses, for example
`--connect '[2001:db8::20]:443' --listen '[2001:db8::10]:5001'`.

Default values are RX, TLS 1.3 AES-128-GCM, one connection, 64 MiB per
connection, 16 KiB application buffers, one warmup and three measured trials.
`-n` specifies bytes **per connection and per active direction**. With
`-d bidir -j 2 -n 256M`, each direction carries 512 MiB and their combined
payload is 1 GiB. `K`, `M`, and `G` are binary multipliers.

`-s` changes the application buffer size, from one byte to 1 MiB. It does not
set TCP MSS or guarantee TLS record boundaries, particularly on the TX path
where HAProxy forms the records. IPv4/IPv6, the link MTU, TCP segmentation and
offloads determine the actual packets reaching the device.

`--cpu N` pins all PC benchmark threads to one CPU if a repeatable CPU placement
is desired. Leave it unset to let multiple connections use multiple cores.
The tool uses one worker per TLS/plaintext connection pair; each worker owns
its SSL object and uses nonblocking I/O for both directions. Watch PC CPU
utilization so data generation/verification does not become the bottleneck.

## Select cipher and key size

`-c` accepts OpenSSL names: a cipher list for TLS 1.2 or a ciphersuite list for
TLS 1.3. The negotiated version and cipher are printed for each trial.

| Test | Options |
| --- | --- |
| TLS 1.2 AES-128-GCM, RSA certificate | `-v 1.2 -c ECDHE-RSA-AES128-GCM-SHA256` |
| TLS 1.2 AES-256-GCM, RSA certificate | `-v 1.2 -c ECDHE-RSA-AES256-GCM-SHA384` |
| TLS 1.3 AES-128-GCM | `-v 1.3 -c TLS_AES_128_GCM_SHA256` |
| TLS 1.3 AES-256-GCM | `-v 1.3 -c TLS_AES_256_GCM_SHA384` |
| TLS 1.3 ChaCha20-Poly1305 | `-v 1.3 -c TLS_CHACHA20_POLY1305_SHA256` |
| TLS 1.3 AES-128-CCM | `-v 1.3 -c TLS_AES_128_CCM_SHA256` |

The selected suite must also be enabled and supported by the proxy's TLS
library. A successful handshake with a particular cipher does not imply that
the proxy can offload that cipher to kTLS. There is no AES-192 TLS ciphersuite.
The default TLS 1.2 cipher list permits ECDHE with either RSA or ECDSA
authentication and AES-128-GCM.

## What is measured and verified

Each trial creates fresh connections. TCP/TLS setup, connection matching,
buffer allocation and generator initialization finish before a common start
gate releases the workers. Elapsed time ends when the last requested payload
byte has been received and verified. A fixed trailer is checked before the
trial can succeed. Setup headers and trailers are excluded from payload byte
counts; waiting for trailers after the last payload byte does not extend the
reported elapsed time.

Each connection and direction has an independent AES-128-CTR byte stream,
derived from `--seed`, trial index, connection index and direction. The sender
and receiver generate their streams independently through the OpenSSL EVP
API. The stream continues across application buffers and short reads/writes,
so it does not repeat a fixed block. Payload generation and comparison happen
inside the timed interval and need only bounded memory, independent of `-n`.
This is a data-pattern generator, not encryption of the plaintext backend.

The default seed is `1708171853`. Keep the seed, warmup count, connection count
and trial index unchanged to reproduce the same payload. Payload corruption
prints, for example:

```text
tls-proxy-bench: trial=2 connection=1: rx payload mismatch: offset=16383 expected=72 actual=f2
```

Offsets count from the beginning of that connection's payload in the named
direction. Corruption, missing data, a wrong trailer, TLS errors, an invalid
setup header or a stalled connection produce a nonzero exit status. A failure
aborts the other workers instead of reporting a successful partial trial.
`--timeout` bounds each connection setup and each worker's no-progress period
(default 10 seconds); it does not limit an otherwise progressing trial.

Output is CSV with comment lines beginning with `#`:

```text
run,direction,connections,bytes_per_connection,seconds,rx_bytes,tx_bytes,rx_gbps,tx_gbps,total_gbps,cpu_seconds,verified
```

RX and TX rates use the same trial elapsed time; `total_gbps` is their sum.
`cpu_seconds` is process CPU time across all PC workers, including the short
trailer exchange and worker teardown. It is not device CPU time. Rates count
verified payload, not TLS/TCP headers or physical link traffic. The final
summary reports min/median/max and total measured bytes divided by total
elapsed time. Warmups are verified but excluded from this summary.

For a single direction, the same payload travels across the PC-device link
once each way. In simultaneous mode both logical flows share each physical
link direction. Interpret the summed throughput with that topology in mind.

## Local validation

```sh
make -C package/utils/tls-proxy-bench/src check
```

The Python test fixture terminates TLS and relays plaintext over loopback. It
checks TLS 1.2/1.3, all directions, concurrent connections, fragmented reads,
short writes, socket backpressure, one-byte buffers, multiple trials, and
AES-128/256-GCM. Fault
injection checks corruption at the beginning, middle and end of the payload,
inserted/dropped bytes, truncated or corrupted trailers, stalls, bad setup
headers and certificate validation. This validates the benchmark, not a
particular proxy's kTLS implementation or EIP93 hardware.
