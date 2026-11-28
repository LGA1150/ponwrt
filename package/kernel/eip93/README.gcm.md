# EIP93 GCM (release 41)

Patch 047 registers `gcm(aes)` with driver name `gcm-aes-eip93` and priority
1501. It uses EIP93 for bulk AES-CTR and a synchronous `ghash` implementation
for authentication. Normal Crypto API selection can choose `ghash-neon`,
including the patched implementation, when it is available. This does not
require GCM hardware support in EIP93.

Generic GCM prepends a separate 16-byte CTR input/output entry to obtain the
tag mask. That short entry fails the EIP93 driver's cache-line isolation
checks, even if the payload is otherwise suitable for direct DMA. This
implementation computes `AES_K(J0)` separately in software, and submits only
payload to `ctr(aes-eip93)`, starting at counter 2. The AAD, tag and final
partial cache line are excluded from the child's SG tables. The common one-
or two-entry SG tables use storage embedded in the request; larger tables
are allocated. Caller SG metadata is never changed.

The bulk length is rounded down to the platform's DMA cache alignment
(at least 16 bytes). Software AES-CTR handles the remaining bytes after
bulk completion. There is no source overread or padded output, and GHASH
uses the original AAD/ciphertext lengths. A 16,384-byte TLS 1.2 payload can
use hardware throughout; the 16,385-byte TLS 1.3 plaintext uses hardware for
16,384 bytes and software for its final byte. The tag mask is a separate
software AES block in both cases. Requests shorter than the DMA alignment
are entirely software. These GCM operations are independent of the optional
size-based `aes_sw_fallback` switch in the child skcipher.

RX authenticates ciphertext before decrypting it. A bad tag returns
`-EBADMSG` without submitting CTR or modifying in-place input. TX computes
GHASH after ciphertext is available. Requests have independent hash, IV and
SG state; child backlog notifications and terminal errors are forwarded to
the AEAD caller. Bulk work uses the existing submission queue and BH
completion workqueue. No `crypto_engine` is added.

## Copies and alignment

An in-place request with cache-line-aligned payload segments and whole-line
bulk extents can use the existing EIP93 direct mapping, avoiding full-record
input and output bounces. This includes an aligned payload in its own SG
entry after a separate AAD entry. Fragmented or misaligned callers remain
supported through the child driver's existing bounce logic and experimental
CTR controls.

`kmod-ktls` release 4 supplies that aligned private working buffer for buffered
asynchronous receives, with its `rx_inplace=1` default. It makes one ciphertext
copy, passes the private buffer in-place and retains the original ciphertext
for KeyUpdate retries. Neither module makes cloned TCP data writable.
`rx_inplace=0` restores release 3's out-of-place receive path for comparison.
Direct-to-userspace kTLS receive keeps its existing path. Existing sockets
keep their allocated AEAD transform; use new connections after loading the
updated modules.

Release 42 and kTLS release 5 add an opt-in
[fragmented-input, aligned-output experiment](README.direct-rx.md) to avoid
all three full-record copies on eligible buffered RX records.

## Testing

Build/install `kmod-eip93` release 41 and `kmod-eip93-test` release 11 for the
same prepared kernel. Verify the registered name in `/proc/crypto`, then run:

```sh
insmod eip93_test 'alg=gcm(aes)' seed=1708171853 repeats=3 stop_on_failure=1
rmmod eip93_test
```

The GCM matrix explicitly selects `gcm-aes-eip93`; the reference explicitly
selects `gcm_base(ctr(aes-generic),ghash-generic)`. It covers all AES key sizes,
all permitted GCM tag lengths, 11 published known answers, fragmented and
unaligned buffers, in-place/out-of-place operation, invalid tags/ciphertext/AAD,
guard regions and lengths from zero through 65,536 bytes. Other algorithms'
case numbers are unchanged.

Host GCC `W=1` builds pass for the complete driver and test modules. A QEMU
TCG fixture compiles the actual GCM wrapper against a software CTR child:
all 1,294 comparisons pass in each of synchronous, BH asynchronous, backlog,
and callback-before-return modes (5,176 total). Separate checks cover 16
requests in flight, rejected submissions, asynchronous errors and unchanged
input on authentication failure. The fixture also checks that each child's
SG extent equals the submitted bulk length and that CTR starts at counter 2.
TLS 1.2 and 1.3 RX recv/splice and TX send/splice pass with two connections
and 257 records per trial, using the same wrapper and asynchronous child.
RX splice uses the benchmark's plaintext preflight and measured length check;
recv and TX trials verify plaintext.

With kTLS release 4, the fixture also passes 32 RX comparisons with
`rx_inplace` off/on, TLS 1.2/1.3, recv/splice and all four completion modes,
plus four TX trials and six TLS boundary/rekey tests. Every buffered RX bulk
request with `rx_inplace=1` has identical child input/output SG pointers and
cache-aligned entry offsets and lengths. The boundary/rekey tests submit 330
such bulk requests with no alignment failures. With the option off, the
child requests remain out-of-place. TLS 1.2 direct-to-userspace recv retains
its previous path in both settings.

The user reports that the release-41 hardware correctness test passed. QEMU
models CTR completion, not the device's DMA behavior; kTLS bounce counts and
throughput with the new aligned allocation remain to be measured. No OpenWrt
package or image build was performed here.
