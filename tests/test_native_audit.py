"""Protocol regression tests; no ROM, runtime, or sockets required."""
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch
import zlib

spec = importlib.util.spec_from_file_location(
    "native_audit", Path(__file__).resolve().parents[1] / "tools/audit_native_items.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
capture_spec = importlib.util.spec_from_file_location(
    "native_capture", Path(__file__).resolve().parents[1] / "tools/prepare_native_capture.py")
capture = importlib.util.module_from_spec(capture_spec)
capture_spec.loader.exec_module(capture)


def connection(reply):
    manager = MagicMock()
    stream = manager.__enter__.return_value.makefile.return_value.__enter__.return_value
    stream.readline.return_value = json.dumps(reply).encode() + b"\n"
    return manager


class NativeAuditProtocolTests(unittest.TestCase):
    def test_fresh_connection_per_request(self):
        first = connection(dict(id=1, ok=True, frame=100))
        second = connection(dict(id=2, ok=True, frame=101))
        with patch.object(audit.socket, "create_connection", side_effect=[first, second]) as connect:
            client = audit.DebugClient(4567)
            self.assertEqual(client.request("frame")["frame"], 100)
            self.assertEqual(client.request("frame")["frame"], 101)
            self.assertEqual(connect.call_count, 2)
        for index, manager in enumerate((first, second), 1):
            payload = json.loads(manager.__enter__.return_value.sendall.call_args.args[0])
            self.assertEqual(payload, dict(id=index, cmd="frame"))
            manager.__exit__.assert_called_once()

    def test_unmatched_or_failed_reply_is_not_evidence(self):
        for reply in [dict(id=0, ok=True, pong=True), dict(id=1, ok=False, err="busy")]:
            with self.subTest(reply=reply):
                with patch.object(audit.socket, "create_connection", return_value=connection(reply)):
                    with self.assertRaises(RuntimeError):
                        audit.DebugClient(4567).request("frame")


class NativeCaptureTests(unittest.TestCase):
    def test_capture_is_verified_additive_and_idempotent(self):
        ram = bytes(2 * 1024 * 1024)
        entry = dict(address="0x80000004", size=4, crc32=f"{zlib.crc32(bytes(4)):08X}", name="Synthetic")
        source = []
        result = capture.append_capture(ram, source, entry)
        self.assertEqual(source, [])
        self.assertEqual(result[0]["function_entry_pcs"], [0x80000004])
        self.assertEqual(capture.append_capture(ram, result, entry), result)
        with self.assertRaises(ValueError):
            capture.append_capture(ram, source, dict(entry, crc32="00000000"))
        self.assertEqual(source, [])

    def test_capture_rejects_bad_ranges_and_truncated_ram(self):
        entry = dict(address="0x80000000", size=4, crc32="00000000", name="Synthetic")
        with self.assertRaises(ValueError):
            capture.append_capture(b"short", [], entry)
        for address, size in [("0x801FFFFC", 8), ("0x80000001", 4), ("0x80000000", 3), ("0x80000000", 0)]:
            with self.subTest(address=address, size=size):
                with self.assertRaises(ValueError):
                    capture.append_capture(bytes(2 * 1024 * 1024), [], dict(entry, address=address, size=size))


if __name__ == "__main__":
    unittest.main()
