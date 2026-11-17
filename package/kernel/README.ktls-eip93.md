# Testing kTLS and EIP93 outside the kernel source tree

These packages build replaceable kernel modules from a private copy of the
target's **prepared Linux 6.18 sources**:

| Package | Build target | Module | Conflicts with |
| --- | --- | --- | --- |
| `kmod-ktls` | `package/kernel/ktls` | `tls.ko` | `kmod-tls` |
| `kmod-eip93` | `package/kernel/eip93` | `crypto-hw-eip93.ko` | `kmod-crypto-hw-eip93` |

Module compilation uses Kbuild's external-module `M=...` interface. Package
patches affect only the private copy. Changing one of these patches therefore
does not require recompiling the kernel itself, provided its configuration and
ABI stay the same. Both packages retain OpenWrt's exact kernel dependency.

The sources are copied at package preparation time, so stable kernel fixes and
PonWrt target patches are retained without downloading a second kernel tree.
The recipes copy only C files, headers and Makefiles, never existing objects.
They require a prepared kernel tree; an SDK without these source directories
is insufficient.

## Contents

`kmod-ktls` carries the module changes from
[`LGA1150/linux`, `tls-rx-pipelining-v6.18`](https://github.com/LGA1150/linux/tree/0734576ba622/net/tls).
The ordered patch series includes:

1. `054dac15f156`: factor record consumption from stream-parser notification.
2. `c6c754edc034`: suppress spurious receive notifications.
3. `147e967dc3c8`: bound consecutive empty records.
4. `1408ac7aaa8a`: consume empty records on splice receive.
5. `fe75db300d8e`: preserve authenticated data before an async receive failure.
6. `ee7de40a610b`: pipeline TLS 1.3 receive decryption.
7. `97f40f2fff04`: reject exhausted transmit record sequences.
8. `2a1919c089b0`: drain pending transmit records before rekeying.
9. `aa81742f2bb1`: pipeline TLS 1.2 splice receive.

The prerequisite fixes precede the pipeline changes. Only `net/tls/` and
`include/net/tls.h` changes are included; the Linux selftests and standalone
benchmark remain in that Linux branch.

This is **software kTLS with Crypto API acceleration**, for TLS 1.2/1.3
`recv`, `send` and `splice`. NIC TLS offload (`CONFIG_TLS_DEVICE`) and TCP stack
bypass (`CONFIG_TLS_TOE`) are disabled. The patched receive-context header is
private to this module and is placed before the base kernel's include paths.
It must not be used with NIC drivers compiled against the original RX layout.

`kmod-eip93` uses the target's existing EIP93 implementation,
including PonWrt's small-request software fallback patch. The fallback
configuration matches the stock package (generic: 256 bytes; AES-128: 512
bytes). Release 4 applies only the three IDR patches for the first hardware test:

1. Reserve and check cipher request IDs before DMA or bounce preparation,
   and release the ID when the request returns synchronously.
2. Reserve hash request IDs before submitting any intermediate blocks,
   and release the ID when no asynchronous completion will follow.
3. Log unmatched completion IDs and skip the NULL request.

Apply the series in filename order. Patch 002 uses the checked ID allocator
from 001. The cipher fix is independent of the broader cleanup and DMA fixes.
Failed ID allocations return before hardware submission or a callback.
Existing DMA mapping, bounce-buffer cleanup, allocation flags and hash
descriptor-publication behavior are retained to isolate IDR handling.
The broader seven-patch series remains available in
[revision 6122d6e](https://github.com/LGA1150/ponwrt/tree/6122d6e4d427a6220bed0668d016ca4864588561/package/kernel/eip93/patches)
for later testing.

This series rejects ID exhaustion with `-ENOSPC` and IDR allocation failure with
`-ENOMEM`. It does not add a software backlog or replace the descriptor-ring
retry loop. If `Missing request:` still appears, retain its ID, flags and status
words: the driver cannot recover the original request from an unmatched ID,
and its caller may remain waiting. The observed crash established a NULL
request lookup; ID exhaustion has not yet been confirmed on hardware.
Add further driver experiments under `package/kernel/eip93/patches/`.
It supports the same Airoha and MT7621 targets as the stock EIP93 package.
AN7581 and AN7583 already have enabled EIP93 device-tree nodes in this fork.
Other boards still need a suitable enabled device-tree node.

For this first test, rebuild and install only `kmod-eip93`, keep stock kTLS,
and rerun `./tls_rx_bench -d tx` with console logging enabled. Retain these
rate-limited messages if they appear:

- `Failed to allocate request ID: -28`: the IDR has no free request IDs.
- `Failed to allocate request ID: -12`: allocation failed with `-ENOMEM`.
- `Missing request: ...`: a completion ID has no matching tracked request;
  retain its ID, flags, control and length words for further diagnosis.

## Select the packages

In `make menuconfig`, select:

- **Kernel modules → Network Support → kmod-ktls**
- **Kernel modules → Cryptographic API modules → kmod-eip93**

Deselect `kmod-tls` and `kmod-crypto-hw-eip93`. For example, the relevant
configuration entries for separately installable packages are:

```text
CONFIG_PACKAGE_kmod-ktls=m
CONFIG_PACKAGE_kmod-eip93=m
# CONFIG_PACKAGE_kmod-tls is not set
# CONFIG_PACKAGE_kmod-crypto-hw-eip93 is not set
```

Run `make defconfig` after editing the configuration. AES-GCM and AES-CCM
dependencies are selected by `kmod-ktls`; EIP93 also selects the cipher and
hash modules needed by its algorithms and software fallbacks.

The package metadata requests `CONFIG_TLS=m`,
`CONFIG_TLS_DEVICE=n`, `CONFIG_TLS_TOE=n` and `CONFIG_CRYPTO_DEV_EIP93=m`.
The in-tree modules may still be compiled during a full kernel build; these
packages install their external replacements instead. Package exclusivity is
handled by the `CONFLICTS` declarations for `kmod-tls` and
`kmod-crypto-hw-eip93`.

If these settings differ from the running firmware, first create and boot a
matching firmware/kernel using the normal PonWrt build procedure. An external
module cannot add missing built-in networking hooks or change the running
kernel's configuration. Keep that build tree for subsequent module builds.

## Rebuild only a module

Once the matching kernel and toolchain have been built, the commands are:

```sh
make package/kernel/ktls/clean V=s
make package/kernel/ktls/compile V=s

make package/kernel/eip93/clean V=s
make package/kernel/eip93/compile V=s
```

Cleaning prepares a fresh private copy and applies the package patches again.
For AN7581, the source copies normally reside in:

```text
build_dir/target-*/linux-airoha_an7581/ktls/net/tls/
build_dir/target-*/linux-airoha_an7581/ktls/include/net/tls.h
build_dir/target-*/linux-airoha_an7581/eip93/
```

Use the corresponding target directory for AN7583 or another supported board.
Use package `prepare QUILT=1`, `quilt new/add/refresh` in the private source
directory, and package `update` to save edits into the package's `patches/`
directory. kTLS patches use paths relative to `ktls/` (such as
`net/tls/tls_sw.c`); EIP93 patches use paths relative to `eip93/` (such as
`eip93-common.c`). Unsaved edits in `build_dir` are lost on `clean`.

Bump the package's `PKG_RELEASE` when distributing an updated build. Keep the
kernel configuration and version unchanged between module iterations. If the
kernel changes, rebuild the modules against that new kernel.

## Install and check

Find the resulting packages under `bin/targets/` or `bin/packages/`:

```sh
find bin -type f \( -name 'kmod-ktls-*.apk' -o -name 'kmod-eip93-*.apk' \)
```

Close connections and other crypto users before replacing loaded modules.
Remove the installed stock package, if present, and install the corresponding
replacement and its dependencies using the device's package manager. For
locally built APKs, `apk add --allow-untrusted /tmp/<package>.apk` permits the
local package signature; it does not bypass the exact kernel dependency.
The packages do not install boot-time autoload entries. Load or reload the
modules explicitly when testing. A busy module cannot be unloaded until its
users have released it.

```sh
modprobe crypto-hw-eip93
modprobe tls
cat /proc/crypto
cat /proc/net/tls_stat
dmesg | tail -n 50
```

EIP93 registration alone does not prove that TLS selected it. The driver
provides AES-CTR and authenc algorithms, not a native `gcm(aes)` or `ccm(aes)`
implementation. Generic GCM/CCM can use its CTR implementation while doing
GHASH/CBC-MAC in software. Check the instantiated AEAD's driver name in
`/proc/crypto`; CPU AES implementations may otherwise win algorithm selection.

The standalone
[`tls_rx_bench.c`](https://github.com/LGA1150/linux/blob/tls-rx-pipelining-v6.18/tools/testing/selftests/net/tls_rx_bench.c)
and existing TLS selftests are available in the Linux branch. The benchmark
accepts `-c gcm|ccm` and `-k 128|256`, with AES-GCM-128 as the default. kTLS
supports AES-GCM-128, AES-GCM-256 and AES-CCM-128 with a 16-byte tag;
AES-192, AES-CCM-256 and CCM-8 are not exposed by its UAPI.

For example, after building the standalone benchmark on your test system:

```sh
./tls_rx_bench -d tx -c gcm -k 256 -j 4
./tls_rx_bench -d tx -c ccm -k 128 -m splice -j 4
./tls_rx_bench -d rx -c ccm -k 128 -v 1.2
```

Run both TLS versions and both directions, including splice, and retain
plaintext verification when comparing results. Cipher selection also applies
to preparation, every worker and the untimed verification pass.

The initial package validation used source/patch and package metadata checks.
Each intermediate version of the three-patch IDR series was compiled with
host GCC 14.2 and `W=1` against prepared Linux 6.18 x86-64 headers, including
PonWrt's fallback
patch and settings. All five driver objects compiled and linked into
`crypto-hw-eip93.o`; this is a host compile check, not a loadable Airoha module
or a hardware runtime test. No firmware, toolchain or benchmark was rebuilt.
