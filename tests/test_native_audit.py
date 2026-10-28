"""Protocol regression tests; no ROM, runtime, or sockets required."""
import importlib.util
import json
from pathlib import Path
import sys
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
newgame_spec = importlib.util.spec_from_file_location(
    "native_newgame", Path(__file__).resolve().parents[1] / "tools/audit_native_newgame.py")
newgame = importlib.util.module_from_spec(newgame_spec)
with patch.dict(sys.modules, {"audit_native_items": audit}):
    newgame_spec.loader.exec_module(newgame)


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

    def test_capture_validates_multiple_roots_and_does_not_silently_drop_them(self):
        ram = bytes(2 * 1024 * 1024)
        entry = dict(address="0x80000004", size=12, crc32=f"{zlib.crc32(bytes(12)):08X}", name="Synthetic",
                     function_entries=["0x80000008", "0x8000000C"])
        result = capture.append_capture(ram, [], entry)
        self.assertEqual(result[0]["function_entry_pcs"], [0x80000008, 0x8000000C])
        self.assertEqual(capture.append_capture(ram, result, entry), result)
        for roots in [[], ["0x80000003"], ["0x80000010"], ["0x80000008", "0x80000008"]]:
            with self.subTest(roots=roots), self.assertRaises(ValueError):
                capture.append_capture(ram, [], dict(entry, function_entries=roots))
        before = capture.append_capture(ram, [], dict(entry, function_entries=["0x80000008"]))
        with self.assertRaises(ValueError):
            capture.append_capture(ram, before, entry)


class NativeNewGameTests(unittest.TestCase):
    def test_signed_state_reads_and_truncated_reply(self):
        process, client = MagicMock(), MagicMock()
        process.poll.return_value = None
        probe = newgame.NewGameProbe(process, client, Path("unused"))
        client.request.return_value = {"hex": "ffff"}
        self.assertEqual(probe.read(0x80071A16, signed=True), -1)
        self.assertEqual(probe.read(0x80071A16), 65535)
        client.request.return_value = {"hex": "ff"}
        with self.assertRaises(RuntimeError):
            probe.read(0x80071A16)

    def test_exited_runtime_cannot_satisfy_state_gate(self):
        process, client = MagicMock(), MagicMock()
        process.poll.return_value = 1
        probe = newgame.NewGameProbe(process, client, Path("unused"))
        predicate = MagicMock(return_value=True)
        with self.assertRaises(RuntimeError):
            probe.wait(predicate, "farm")
        predicate.assert_not_called()
        client.request.assert_not_called()

    def test_failed_input_gate_releases_button(self):
        process, client = MagicMock(), MagicMock()
        process.poll.return_value = None
        probe = newgame.NewGameProbe(process, client, Path("unused"))
        with patch.object(probe, "wait", side_effect=RuntimeError("state failed")):
            with self.assertRaises(RuntimeError):
                probe.press_until(0xFFF7, lambda: False, "title")
        self.assertEqual(client.request.call_args_list[0].args, ("set_input",))
        self.assertEqual(client.request.call_args_list[-1].args, ("clear_input",))


if __name__ == "__main__":
    unittest.main()
