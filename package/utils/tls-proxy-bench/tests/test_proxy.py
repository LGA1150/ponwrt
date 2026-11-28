#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the benchmark through a TLS-terminating, fault-injecting proxy.

This checks the userspace harness, not HAProxy splicing or a hardware driver.
Only loopback sockets and a temporary certificate directory are used.
"""

import asyncio
import csv
import io
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile


class Proxy:
    def __init__(self, context, fault=None, direction="rx", offset=0, fragment=0, delay=0):
        self.context = context
        self.fault = fault
        self.direction = direction
        self.offset = offset
        self.fragment = fragment
        self.delay = delay
        self.backend_port = None
        self.tasks = set()

    async def __aenter__(self):
        self.server = await asyncio.start_server(
            self.handle, "127.0.0.1", 0, ssl=self.context)
        self.port = self.server.sockets[0].getsockname()[1]
        return self

    async def __aexit__(self, *_args):
        self.server.close()
        await self.server.wait_closed()
        for task in list(self.tasks):
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)

    async def copy(self, reader, writer, direction):
        position = 0
        injected = False
        target = self.offset + (64 if direction == "rx" else 0)
        while True:
            data = await reader.read(8191)
            if not data:
                return
            if self.delay:
                await asyncio.sleep(self.delay)
            old_position = position
            position += len(data)
            if (self.fault and direction == self.direction and not injected
                    and old_position <= target < position):
                injected = True
                index = target - old_position
                if self.fault == "corrupt":
                    data = data[:index] + bytes([data[index] ^ 0x80]) + data[index + 1:]
                elif self.fault == "insert":
                    data = data[:index] + b"\xff" + data[index:]
                elif self.fault == "drop":
                    data = data[:index] + data[index + 1:]
                elif self.fault in ("truncate", "stall"):
                    writer.write(data[:index])
                    await writer.drain()
                    if self.fault == "truncate":
                        writer.close()
                        return
                    await asyncio.Event().wait()
                else:
                    raise AssertionError(self.fault)
            step = self.fragment or max(1, len(data))
            for start in range(0, len(data), step):
                writer.write(data[start:start + step])
                await writer.drain()

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        backend = None
        copies = []
        try:
            while self.backend_port is None:
                await asyncio.sleep(0.001)
            backend_reader, backend = await asyncio.open_connection(
                "127.0.0.1", self.backend_port)
            copies = [asyncio.create_task(self.copy(reader, backend, "rx")),
                      asyncio.create_task(self.copy(backend_reader, writer, "tx"))]
            await asyncio.gather(*copies)
        except (ConnectionError, ssl.SSLError, asyncio.CancelledError):
            pass
        finally:
            for copy in copies:
                copy.cancel()
            await asyncio.gather(*copies, return_exceptions=True)
            writer.close()
            if backend:
                backend.close()
            self.tasks.discard(task)


async def run(binary, cert, context, label, args, expected=None, **proxy_args):
    async with Proxy(context, **proxy_args) as proxy:
        command = [binary, "--connect", f"127.0.0.1:{proxy.port}",
                   "--listen", "127.0.0.1:0", "--ca", str(cert),
                   "--server-name", "localhost", "--timeout", "2",
                   "-n", "65537", "-r", "1", "-w", "0", *args]
        proc = await asyncio.create_subprocess_exec(
            *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        try:
            first = await asyncio.wait_for(proc.stdout.readline(), 5)
            assert first.startswith(b"# listening="), (label, first)
            proxy.backend_port = int(first.decode().strip().rsplit(":", 1)[1])
            out, err = await asyncio.wait_for(proc.communicate(), 30)
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()
        out = (first + out).decode()
        err = err.decode()
        if expected is not None:
            assert proc.returncode != 0, (label, "false PASS", out, err)
            assert "# PASS" not in out, (label, out)
            assert expected in err, (label, expected, err, out)
        else:
            assert proc.returncode == 0, (label, proc.returncode, out, err)
            assert "# PASS" in out and "verified=payload" in out, (label, out)
            rows = list(csv.DictReader(io.StringIO(
                "\n".join(line for line in out.splitlines() if not line.startswith("#")))))
            assert rows, (label, out)
            for row in rows:
                size = int(row["bytes_per_connection"]) * int(row["connections"])
                assert int(row["rx_bytes"]) == (size if row["direction"] != "tx" else 0)
                assert int(row["tx_bytes"]) == (size if row["direction"] != "rx" else 0)
                assert float(row["seconds"]) > 0
                assert row["verified"] == "payload"
        print(f"PASS {label}", flush=True)


async def main(binary, cert, key):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    for version in ("1.2", "1.3"):
        for direction in ("rx", "tx", "bidir"):
            await run(binary, cert, context, f"TLS {version} {direction} fragmented",
                      ["-v", version, "-d", direction, "-j", "3", "-s", "16383"],
                      fragment=127)
    await run(binary, cert, context, "single-byte IO", ["-d", "bidir", "-n", "257", "-s", "1"])
    await run(binary, cert, context, "partial TLS writes and repeated trials",
              ["-d", "bidir", "-n", "4M", "-s", "1M", "-j", "4", "-w", "1", "-r", "2"])
    await run(binary, cert, context, "bidirectional socket backpressure",
              ["-d", "bidir", "-n", "8M", "-s", "1M", "-j", "2"], delay=0.001)
    await run(binary, cert, context, "TLS 1.3 AES-256", ["-c", "TLS_AES_256_GCM_SHA384"])
    await run(binary, cert, context, "TLS 1.2 AES-256",
              ["-v", "1.2", "-c", "ECDHE-RSA-AES256-GCM-SHA384", "-d", "tx"])
    for direction in ("rx", "tx"):
        for offset in (0, 16383, 65536):
            await run(binary, cert, context, f"{direction} corruption at {offset}",
                      ["-d", "bidir", "-j", "3"], fault="corrupt", direction=direction,
                      offset=offset, expected=f"{direction} payload mismatch: offset={offset} ")
        for fault in ("insert", "drop"):
            await run(binary, cert, context, f"{direction} {fault}", ["-d", direction],
                      fault=fault, direction=direction, offset=71, expected="payload mismatch")
        await run(binary, cert, context, f"{direction} truncated trailer", ["-d", direction],
                  fault="truncate", direction=direction, offset=65537, expected="EOF")
        await run(binary, cert, context, f"{direction} bad trailer", ["-d", direction],
                  fault="corrupt", direction=direction, offset=65537, expected="trailer mismatch")
        await run(binary, cert, context, f"{direction} stall", ["-d", direction],
                  fault="stall", direction=direction, offset=3, expected="stalled")
    await run(binary, cert, context, "setup header corruption", [], fault="corrupt",
              offset=-40, expected="setup header mismatch")
    await run(binary, cert, context, "wrong server identity",
              ["--server-name", "wrong.example"], expected="certificate verify failed")
    await run(binary, cert, context, "explicit lab certificate bypass",
              ["--server-name", "wrong.example", "--insecure"])


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="tls-proxy-bench-") as directory:
        cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(key), "-out", str(cert), "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        asyncio.run(main(binary, cert, key))
