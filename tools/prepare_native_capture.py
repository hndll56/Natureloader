#!/usr/bin/env python3
"""Append one verified native entry from a player-owned 2 MiB RAM dump.

The output contains game code. Keep it in ignored local build directories.
It feeds the research runtime's compile_overlays.py, not the mod installer.
"""
import argparse
import base64
import json
from pathlib import Path
import zlib


def append_capture(ram, captures, entry):
    if len(ram) != 2 * 1024 * 1024 or not isinstance(captures, list):
        raise ValueError("Expected a 2 MiB RAM dump and a capture array")
    address = int(entry["address"], 0)
    size = entry["size"]
    if not isinstance(size, int) or not 0 < size <= 65536 or address % 4 or size % 4:
        raise ValueError("Invalid aligned capture range")
    if not 0x80000000 <= address < 0x80200000 or address + size > 0x80200000:
        raise ValueError("Capture lies outside guest RAM")
    offset = address - 0x80000000
    data = ram[offset:offset + size]
    if zlib.crc32(data) != int(entry["crc32"], 16):
        raise ValueError("RAM bytes do not match the audited entry CRC; wrong overlay or game")
    encoded = base64.b64encode(data).decode("ascii")
    for old in captures:
        if (int(old["load_addr"], 0) == address and old["size"] == size
                and old["bytes_b64"] == encoded):
            return list(captures)
    return [*captures, {
        "schema": "psxrecomp overlay capture v2", "load_addr": hex(address),
        "size": size, "bytes_b64": encoded,
        "producer": "NatureLoader verified local RAM capture", "producer_name": entry["name"],
        "dispatch_entry_pcs": [address], "static_dispatch_entry_pcs": [address],
        "function_entry_pcs": [address],
    }]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ram", type=Path, required=True)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--entry", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads((Path(__file__).parent / "native-patches/coverage_entries.json").read_text())
    entry = next((entry for entry in metadata["entries"] if int(entry["address"], 0) == args.entry), None)
    if entry is None:
        parser.error("Entry has not been audited")
    result = append_capture(args.ram.read_bytes(), json.loads(args.captures.read_text()), entry)
    # Exclusive creation preserves existing captures, including on user error.
    with args.output.open("x") as output:
        json.dump(result, output)
        output.write("\n")
    print(f"Prepared {len(result)} captures in {args.output}; keep this generated file local.")


if __name__ == "__main__":
    main()
