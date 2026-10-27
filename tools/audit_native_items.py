#!/usr/bin/env python3
"""Audit stock/modded/restored retail item RAM in the local native runtime.

This proves live table writes and restoration, not item interactions, new
entities, farm gameplay, or save compatibility. No ROM data is distributed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import zipfile


class DebugClient:
    """The research debugger closes its socket after each JSON request."""

    def __init__(self, port, timeout=5):
        self.port = port
        self.timeout = timeout
        self.sequence = 0

    def request(self, command, **fields):
        self.sequence += 1
        payload = dict(id=self.sequence, cmd=command, **fields)
        with socket.create_connection(("127.0.0.1", self.port), self.timeout) as connection:
            connection.settimeout(self.timeout)
            connection.sendall((json.dumps(payload) + "\n").encode())
            with connection.makefile("rb") as stream:
                line = stream.readline(1024 * 1024)
        if not line or not line.endswith(b"\n"):
            raise RuntimeError("Incomplete debugger response")
        reply = json.loads(line)
        if reply.get("id") != self.sequence or reply.get("ok") is not True:
            raise RuntimeError(f"Debugger {command}: {reply}")
        return reply


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


FIELDS = [
    ("turnip.sell_price", 0x8004BBDE, 2),
    ("turnip.stamina", 0x8004BBEC, 1),
    ("turnip.fatigue", 0x8004BBED, 1),
    ("turnip-seeds.buy_price", 0x8004B704, 2),
    ("sickle.stamina", 0x8004B1EC, 1),
]


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def audit_run(args, temporary, label, snapshot, expected):
    # Closing the reservation leaves a small bind race. Startup checks below
    # report that failure rather than attaching to a different runtime.
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    client = DebugClient(port)
    environment = os.environ.copy()
    environment["NATURE_MOD_DIRECTORY"] = snapshot
    log_path = temporary / (label + ".log")
    with log_path.open("wb") as log:
        process = subprocess.Popen([
            str(args.runtime), "--headless", "--no-launcher", "--renderer", "software",
            "--game", str(args.config), "--disc", str(args.disc),
            "--memcard-dir", str(temporary / (label + "-saves")), "--debug-port", str(port),
        ], cwd=args.runtime.parent, env=environment, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + args.timeout
            while True:
                if process.poll() is not None:
                    raise RuntimeError(f"Runtime exited before item table initialized: {process.returncode}")
                if time.monotonic() >= deadline:
                    raise RuntimeError("Timed out waiting for game_state.itemInfo")
                try:
                    pointer = client.request("read_ram", addr="0x8006C858", len=4)
                except ConnectionRefusedError:
                    time.sleep(.1)
                    continue
                if pointer.get("hex") == "a4b10480":
                    break
                time.sleep(.1)
            observed = []
            for _, address, size in FIELDS:
                data = bytes.fromhex(client.request("read_ram", addr=hex(address), len=size)["hex"])
                if len(data) != size:
                    raise RuntimeError("Truncated item RAM read")
                observed.append(int.from_bytes(data, "little", signed=True))
            if observed != expected:
                raise RuntimeError(f"{label}: got {observed}, expected {expected}")
            first = client.request("frame")["frame"]
            time.sleep(.2)
            last = client.request("frame")["frame"]
            if last <= first:
                raise RuntimeError("Runtime frames stopped advancing")
            stats = client.request("dirty_ram_stats")
            if stats["insns_run"] != 0:
                raise RuntimeError("Runtime used interpreted instructions")
            if process.poll() is not None:
                raise RuntimeError("Runtime exited during audit")
            return dict(run=label, values=dict(zip([f[0] for f in FIELDS], observed)),
                        frame=last, interpreted_instructions=stats["insns_run"])
        except Exception as error:
            stop(process)
            log.flush()
            raise RuntimeError(f"{label}: {error}\n{log_path.read_text(errors='replace')[-6000:]}") from error
        finally:
            # SIGTERM is intentional cleanup, never evidence of a boot failure.
            stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("launcher", "runtime", "config", "disc", "exe"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    if not 0 < args.timeout <= 600:
        parser.error("--timeout must be in (0, 600]")
    for name in ("launcher", "runtime", "config", "disc", "exe"):
        path = getattr(args, name).resolve(strict=True)
        if not path.is_file():
            parser.error(f"--{name} must be a regular file")
        setattr(args, name, path)
    original = digest(args.exe)
    results = []
    with tempfile.TemporaryDirectory(prefix="nature-native-audit-") as directory:
        temporary = Path(directory)

        def cli(*command):
            output = subprocess.run([str(args.launcher), "--data", str(temporary / "library"), *command],
                                    capture_output=True, text=True, timeout=30, check=True)
            return json.loads(output.stdout)

        stock = cli("--native-export", str(args.exe))["directory"]
        package = temporary / "balance.zip"
        with zipfile.ZipFile(package, "w") as archive:
            archive.write(Path(__file__).resolve().parents[1] / "examples/native-balance-v2/mod.json", "mod.json")
        cli("--install", str(package))
        cli("--enable", "native-balance-v2")
        modded = cli("--native-export", str(args.exe))["directory"]
        cli("--disable", "native-balance-v2")
        restored = cli("--native-export", str(args.exe))["directory"]
        for label, snapshot, expected in [
            ("stock", stock, [60, 3, -1, 120, -2]),
            ("modded", modded, [120, 6, -2, 80, -1]),
            ("restored", restored, [60, 3, -1, 120, -2]),
        ]:
            result = audit_run(args, temporary, label, snapshot, expected)
            results.append(result)
            print(json.dumps(result), flush=True)
    if digest(args.exe) != original:
        raise RuntimeError("Source executable changed")
    print(json.dumps(dict(ok=True, runs=len(results), source_executable_unchanged=True,
                          scope="live retail table RAM; not new item/NPC gameplay")), flush=True)


if __name__ == "__main__":
    main()
