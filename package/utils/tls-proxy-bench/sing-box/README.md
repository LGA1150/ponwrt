# sing-box kTLS splice experiment

This recipe forwards `tls-proxy-bench` through sing-box's Trojan fallback.
The benchmark deliberately sends a non-Trojan setup header, causing sing-box
to forward the original stream to a fixed plaintext backend. It tests TLS
termination and forwarding, not authenticated Trojan framing or multiplexing.

## Apply the feed patches

The patches target **sing-box v1.14.3** and apply in this order:

| Patch | Change |
| --- | --- |
| `110-allow-server-side-ktls-rx-splice.patch` | Removes the TLS-server role restriction from the RX splice accessor, retaining the `kernel_rx` requirement. |
| `111-drain-buffered-plaintext-before-rx-splice.patch` | Exposes unread TLS plaintext through `network.CachedReader` so forwarding drains it before splice; refuses raw RX access while plaintext remains buffered. Includes regression tests. |

Both patches are needed for this experiment. Removing only the role check
can strand plaintext in sing-box's userspace TLS buffer: Trojan reads 56 bytes
from the benchmark's 64-byte setup record, then splice bypasses the remaining
eight bytes. The backend waits for its complete setup header and times out,
including when the subsequent payload direction is TX.

The cache hook returns an owned copy because the TLS record storage is reused
on later reads. This copies only already-buffered plaintext at the handoff;
subsequent eligible records can use splice.

PonWrt obtains sing-box from the external ImmortalWrt packages feed. After
populating that feed and checking its sing-box version, run from the PonWrt
root:

```sh
./scripts/feeds install sing-box
cp package/utils/tls-proxy-bench/sing-box/patches/*.patch \
    feeds/packages/net/sing-box/patches/
make package/feeds/packages/sing-box/clean
make package/feeds/packages/sing-box/compile V=s
```

The benchmark package does not apply these patches automatically. Keep them
when updating the feed. The sing-box build needs Linux, Go 1.25 or newer,
the `badlinkname` build tag and `-checklinkname=0` linker flag; the tested
ImmortalWrt sing-box v1.14.3 package already supplies those flags.

## Configure the router

Copy [sing-box.json](sing-box.json) to `/tmp/ktls/sing-box.json` on the router.
The example uses the existing lab certificate and private key at
`/tmp/ktls/haproxy/server.crt.pem` and `/tmp/ktls/haproxy/server.key.pem`.
Adjust those paths and the addresses for your setup:

- Router listener: `192.0.2.20:8444`.
- PC plaintext backend: `192.0.2.10:5001`.
- TLS 1.3, `kernel_tx=true`, `kernel_rx=true`.
- No additional transport or multiplexing layer.

The example addresses are documentation placeholders. Substitute the router's
and PC's reachable addresses before starting the test.

sing-box's kTLS integration supports TLS 1.3 here. Start the patched binary:

```sh
sing-box check -c /tmp/ktls/sing-box.json
sing-box run -c /tmp/ktls/sing-box.json > /tmp/ktls/sing-box.log 2>&1 &
echo $! > /tmp/ktls/sing-box.pid
```

The PC benchmark starts its own backend listener. Permit the router to connect
to that listener, then run on the PC:

```sh
# Router decrypts and forwards to the plaintext backend.
./tls-proxy-bench --connect 192.0.2.20:8444 \
    --listen 0.0.0.0:5001 --insecure \
    -d rx -v 1.3 -j 2 -n 256M -w 1 -r 5

# Router encrypts data sent by the plaintext backend.
./tls-proxy-bench --connect 192.0.2.20:8444 \
    --listen 0.0.0.0:5001 --insecure \
    -d tx -v 1.3 -j 2 -n 256M -w 1 -r 5
```

`--insecure` accommodates the lab's self-signed certificate. For certificate
verification, use the benchmark's `--ca` and `--server-name` options instead.

Check debug logs for `kernel TLS RX enabled`, `kernel TLS TX enabled`,
`RX splice requested` and `TX splice requested`. A passing benchmark verifies
payload delivery; it does not independently prove that every record used
splice. For a separate short diagnostic run, trace the router process:

```sh
strace -f -e trace=splice -p "$(cat /tmp/ktls/sing-box.pid)"
```

Detach the trace before measuring throughput.

## Compare ChaCha20-Poly1305

Select the TLS 1.3 ciphersuite explicitly and keep the other benchmark options
unchanged:

```sh
./tls-proxy-bench --connect 192.0.2.20:8444 \
    --listen 0.0.0.0:5001 --insecure \
    -d rx -v 1.3 -c TLS_CHACHA20_POLY1305_SHA256 \
    -j 2 -n 256M -w 1 -r 5
```

Use `-d tx` for the transmit comparison. Check that the benchmark reports
`cipher=TLS_CHACHA20_POLY1305_SHA256`, and check sing-box's kernel TLS enablement
logs for these new connections. The kernel needs its ChaCha20-Poly1305 crypto
implementation available. This measures CPU crypto on this device; the EIP93
AES engine does not accelerate ChaCha20-Poly1305. No ChaCha20 measurements are
included in the AES-GCM results below.

## Validation and device results

The local TCP-splice regression test models an already-buffered TLS record
and the Trojan fallback wrapper. With patch 110 alone, it forwards 1,464 of
1,472 expected bytes, reproducing the missing eight bytes. Patch 111 fixes
that loss. Both regression tests pass 20 repetitions with Go's race detector,
including buffer ownership and raw RX availability before and after draining:

```sh
go test -tags badlinkname -ldflags=-checklinkname=0 -race -count=20 \
    -run 'TestCopyBufferedTLSRecordBeforeSplice|TestReadCachedBeforeRawSocket' \
    ./common/ktls
```

The tests use plaintext TCP sockets to model the kTLS socket handoff and do
not require kernel TLS keys. The patches also pass clean application and
whitespace checks against v1.14.3. No OpenWrt package/image build was performed
as part of preparing these patches.

The user subsequently reported these device results on 2026-11-26, using the
AN7581/Cortex-A53 lab setup and patched sing-box. The PC reports OpenSSL 3.5.5;
all trials negotiated TLS 1.3 `TLS_AES_128_GCM_SHA256`. Each direction used
two connections, 256 MiB per connection, 16 KiB application buffers, one
verified warmup and five measured trials, with seed 1708171853.

| Direction relative to router | Min Gbps | Median Gbps | Max Gbps | Aggregate Gbps |
| --- | ---: | ---: | ---: | ---: |
| RX | 0.743895 | 0.753308 | 0.766852 | 0.755270 |
| TX | 0.729753 | 0.731664 | 0.737203 | 0.733442 |

Both directions passed every payload check: 3 GiB per direction including
warmup. The setup timeout was resolved. Raw user-reported output is retained
in [RX results](results/2026-11-26-rx.csv) and
[TX results](results/2026-11-26-tx.csv). The `cpu_seconds` column measures the
PC benchmark process, not the router. These runs cover the fallback proxy
path and do not establish coverage of TLS KeyUpdate or authenticated Trojan
sessions.
