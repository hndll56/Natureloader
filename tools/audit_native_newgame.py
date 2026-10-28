#!/usr/bin/env python3
"""Probe native New Game coverage with state-aware input and private saves.

This is a coverage probe, not proof of farming, new mod items, NPCs or saves.
Evidence contains private game screenshots/RAM and must stay outside commits.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

from audit_native_items import DebugClient, digest, stop


USA_EXE = "34e3b35dcd3fb1cd9a7d4813b8a9ef987915826e3d436f1908ecbe2d4fdf1444"
SCENE = 0x8005E39C
TITLE_STATE = 0x800C4A24
TITLE_ROW = 0x800C4A1A
NAME_STATE = 0x8005E344
BIRTHDAY_STATE = 0x8005E338
MENU_STATE = 0x8005E5B4
PLAYER_FIELDS = {
    "stamina": (0x80071A12, 2), "max_stamina": (0x80071A14, 2),
    "fatigue": (0x80071A16, 2), "money": (0x80071A5C, 4),
    "year": (0x80070CF0, 2), "season": (0x80070CF2, 2), "day": (0x80070CF4, 2),
}


class NewGameProbe:
    def __init__(self, process, client, evidence, timeout=30):
        self.process, self.client, self.evidence = process, client, evidence
        self.timeout = timeout
        self.phases = []

    def alive(self):
        if self.process.poll() is not None:
            raise RuntimeError(f"Native runtime exited with code {self.process.returncode}")

    def read(self, address, size=2, signed=False):
        self.alive()
        raw = bytes.fromhex(self.client.request("read_ram", addr=hex(address), len=size)["hex"])
        if len(raw) != size:
            raise RuntimeError("Truncated state read")
        return int.from_bytes(raw, "little", signed=signed)

    def press(self, buttons=0xBFFF):
        self.alive()
        self.client.request("set_input", buttons=hex(buttons))
        time.sleep(.2)
        self.client.request("clear_input")
        time.sleep(.7)

    def press_until(self, buttons, predicate, label):
        self.alive()
        self.client.request("set_input", buttons=hex(buttons))
        try:
            self.wait(predicate, label, timeout=5)
        finally:
            if self.process.poll() is None:
                self.client.request("clear_input")
        time.sleep(.3)

    def wait(self, predicate, label, advance=False, timeout=None):
        deadline = time.monotonic() + (timeout or self.timeout)
        while time.monotonic() < deadline:
            self.alive()
            try:
                if predicate():
                    return
            except ConnectionRefusedError:
                time.sleep(.1)
                continue
            if advance:
                self.press()
            else:
                time.sleep(.01)
        raise RuntimeError(f"Timed out waiting for {label}")

    def record(self, label):
        self.alive()
        self.client.request("screenshot_file", path=str(self.evidence / (label + ".png")))
        state = dict(phase=label, frame=self.client.request("frame")["frame"],
                     scene=self.read(SCENE, 4), name=self.read(NAME_STATE),
                     birthday=self.read(BIRTHDAY_STATE), menu=self.read(MENU_STATE))
        if state["scene"] == 2:
            state.update(player_flags=hex(self.read(0x80071A6C, 4)),
                         script_running=self.read(0x80071A26), player_mode=self.read(0x80071A3C),
                         position=[self.read(a, 4, signed=True) for a in (0x80071B00, 0x80071B04, 0x80071B08)])
        self.phases.append(state)
        print(json.dumps(state), flush=True)

    def menu_ready(self):
        self.wait(lambda: self.read(MENU_STATE) in (2, 5), "name/birthday menu ready")

    def name(self, label):
        self.wait(lambda: self.read(NAME_STATE) == 1, label + " editor", advance=True)
        self.menu_ready()
        self.record(label + "-editor")
        self.press()
        self.press_until(0xFFF7, lambda: self.read(NAME_STATE) == 2, label + " confirmation")
        self.menu_ready()
        self.record(label + "-confirm")
        self.press_until(0xBFFF, lambda: self.read(NAME_STATE) == 0, label + " accepted")

    def setup(self):
        self.wait(lambda: self.read(SCENE, 4) == 13 and self.read(TITLE_STATE) == 2,
                  "title intro", timeout=60)
        self.press_until(0xFFF7, lambda: self.read(TITLE_STATE) != 2, "intro Start accepted")
        self.wait(lambda: self.read(TITLE_STATE) == 6, "Press Start")
        self.press_until(0xFFF7, lambda: self.read(TITLE_STATE) == 7, "title menu")
        self.record("title-menu")
        # Use a short guest-frame pulse: a long held direction may repeat and
        # toggle this two-row menu twice before the next debugger read.
        for _ in range(5):
            self.client.request("press", buttons=0xFFEF, frames=8)
            time.sleep(.7)
            if self.read(TITLE_ROW) == 0:
                break
        self.wait(lambda: self.read(TITLE_ROW) == 0, "New Game selection", timeout=2)
        self.record("newgame-selected")
        self.press()
        self.name("player")
        self.wait(lambda: self.read(BIRTHDAY_STATE) == 1, "birthday season", advance=True)
        self.menu_ready()
        self.press_until(0xBFFF, lambda: self.read(BIRTHDAY_STATE) == 2, "birthday day")
        self.menu_ready()
        self.press_until(0xBFFF, lambda: self.read(BIRTHDAY_STATE) == 3, "birthday confirmation")
        self.menu_ready()
        self.record("birthday-confirm")
        self.press_until(0xBFFF, lambda: self.read(BIRTHDAY_STATE) == 0, "birthday accepted")
        self.name("farm")
        self.name("dog")
        self.wait(lambda: self.read(TITLE_STATE) == 26, "final setup summary")
        self.record("summary")
        # The final Yes/No prompt uses menu phase 7, unlike the name editor.
        self.wait(lambda: self.read(SCENE, 4) != 13, "leaving title", advance=True)
        if self.read(SCENE, 4) != 2:
            raise RuntimeError("New Game did not enter the farm scene")
        self.record("farm-transition")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "config", "disc", "exe", "evidence-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--observe-seconds", type=float, default=15)
    parser.add_argument("--movement", action="store_true", help="Also require live player position to change after Right")
    args = parser.parse_args()
    if not 5 <= args.observe_seconds <= 60:
        parser.error("--observe-seconds must be 5..60")
    for name in ("runtime", "config", "disc", "exe"):
        path = getattr(args, name).resolve(strict=True)
        if not path.is_file():
            parser.error(f"--{name} must be a regular file")
        setattr(args, name, path)
    original = digest(args.exe)
    if original != USA_EXE:
        parser.error("Unsupported executable; USA hash mismatch")
    evidence = args.evidence_dir.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    env = os.environ.copy()
    env["NATURE_MOD_DIRECTORY"] = str(evidence / "empty-mods")
    env["NATURE_NATIVE_GAP_DUMP"] = str(evidence / "gap.bin")
    result = dict(ok=False, scope="native New Game coverage probe", farm_gameplay_verified=False)
    with (evidence / "runtime.log").open("wb") as log:
        process = subprocess.Popen([
            str(args.runtime), "--headless", "--no-launcher", "--renderer", "software",
            "--game", str(args.config), "--disc", str(args.disc),
            "--memcard-dir", str(evidence / "private-saves"), "--debug-port", str(port),
        ], cwd=args.runtime.parent, env=env, stdout=log, stderr=log)
        probe = NewGameProbe(process, DebugClient(port), evidence)
        try:
            probe.setup()
            first = probe.client.request("frame")["frame"]
            deadline = time.monotonic() + args.observe_seconds
            while time.monotonic() < deadline:
                probe.alive()
                time.sleep(.1)
            if probe.read(SCENE, 4) != 2:
                raise RuntimeError("Runtime left the farm scene during observation")
            last = probe.client.request("frame")["frame"]
            interpreted = probe.client.request("dirty_ram_stats")["insns_run"]
            if last <= first or interpreted != 0:
                raise RuntimeError("Frames stopped or interpreter was used")
            probe.record("after-observation")
            if args.movement:
                # The mayor's opening tour controls the player. Finish dialogue
                # through actual confirm input before asserting free movement.
                probe.wait(lambda: probe.read(SCENE, 4) == 2 and
                           (probe.read(0x80071A6C, 4) & ((1 << 1) | (1 << 10) | (1 << 23))) == 0,
                           "opening event to release player control", advance=True, timeout=180)
                probe.record("player-control")
                def position():
                    return [probe.read(address, 4, signed=True) for address in (0x80071B00, 0x80071B04, 0x80071B08)]
                before = position()
                probe.press_until(0xFFDF, lambda: position() != before, "player movement")
                after = position()
                result.update(position_before=before, position_after=after)
                probe.record("after-movement")
                if probe.read(SCENE, 4) != 2 or before == after:
                    raise RuntimeError("Movement input did not change the farm player's position")
                if probe.client.request("dirty_ram_stats")["insns_run"] != 0:
                    raise RuntimeError("Interpreter used during movement")
                result["movement_verified"] = True
            result.update(ok=True, first_frame=first, last_frame=last, interpreted_instructions=interpreted,
                          player={key: probe.read(address, size, signed=True)
                                  for key, (address, size) in PLAYER_FIELDS.items()})
        except Exception as error:
            result["error"] = str(error)
            if process.poll() is None:
                try:
                    probe.record("failure")
                    result["interpreted_instructions"] = probe.client.request("dirty_ram_stats")["insns_run"]
                except Exception as diagnostic_error:
                    result["diagnostic_error"] = str(diagnostic_error)
        finally:
            stop(process)
        result["phases"] = probe.phases
    text = (evidence / "runtime.log").read_text(errors="replace")
    gaps = re.findall(r"NATURE_NATIVE_COVERAGE_GAP[^\r\n]*", text)
    if gaps:
        result.update(ok=False, coverage_gap=gaps[-1])
    result["source_executable_unchanged"] = digest(args.exe) == original
    if not result["source_executable_unchanged"]:
        result.update(ok=False, error="Source executable changed")
    (evidence / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
