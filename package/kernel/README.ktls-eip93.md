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

`kmod-eip93` initially uses the target's existing EIP93 implementation,
including PonWrt's small-request software fallback patch. The fallback
configuration matches the stock package (generic: 256 bytes; AES-128: 512
bytes). Add driver experiments under `package/kernel/eip93/patches/`.
It supports the same Airoha and MT7621 targets as the stock EIP93 package.
AN7581 and AN7583 already have enabled EIP93 device-tree nodes in this fork.
Other boards still need a suitable enabled device-tree node.

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

The standalone `tls_rx_bench.c` and existing TLS selftests are available in
the Linux branch linked above. Run both TLS versions and both directions,
including splice, and retain plaintext verification when comparing results.

No kernel, module, toolchain, benchmark or selftest build was run when adding
these packages. Validation was limited to source/patch and package metadata
checks.
