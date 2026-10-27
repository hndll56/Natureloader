"""Optional local integration check. Requires the user's ROM; never downloads one."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    executable, rom = (Path(arg).resolve() for arg in sys.argv[1:3])
    original_hash = digest(rom)
    with tempfile.TemporaryDirectory(prefix="nature-integration-") as directory:
        root = Path(directory)

        def run(*args, success=True):
            result = subprocess.run(
                [str(executable), "--data", str(root / "library"), *args],
                capture_output=True, text=True, encoding="utf-8", timeout=90,
            )
            if not success:
                assert result.returncode != 0, result.stdout
                return
            assert result.returncode == 0, result.stderr
            return json.loads(result.stdout)

        report = run("--import", str(rom))
        assert report["textCount"] == 354
        assert len(report["members"]) == 32
        base = root / "library" / "base"
        hashes_before = {path.name: digest(path) for path in base.iterdir()}
        key = "messages.promptYesNo"
        original = run("--text", key)
        assert original["provider"] == "ROM"
        assert "Yes" in original["text"] and "No" in original["text"]
        for name, text in [("first", "Halo dari mod pertama"), ("second", "Mod kedua menang")]:
            manifest = {
                "schema": 1, "id": name, "name": name, "version": "1.0",
                "game": "SLUS_011.15", "texts": {key: text},
            }
            archive = root / f"{name}.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
                output.writestr("mod.json", json.dumps(manifest))
                output.writestr("LICENSE", "Example mod license retained during installation.")
            run("--install", str(archive))
            assert (root / "library" / "mods" / name / "LICENSE").is_file()
            run("--enable", name)
            assert run("--text", key) == {"key": key, "provider": name, "text": text}
        assert len(run("--status")["conflicts"]) == 1
        run("--disable", "second")
        assert run("--text", key)["provider"] == "first"
        run("--disable", "first")
        assert run("--text", key) == original
        # Native exporter rejects long retail text instead of silently truncating it.
        run("--enable", "first")
        run("--native-export", str(base / "SLUS_011.15"), success=False)
        run("--disable", "first")
        native_manifest = {"schema": 1, "id": "native-ui", "name": "UI", "version": "1.0",
                           "game": "SLUS_011.15", "texts": {"messages.selectAMemoryCardSlot": "Pilih slot\nNatureLoader!"}}
        archive = root / "native-ui.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("mod.json", json.dumps(native_manifest))
        run("--install", str(archive))
        run("--enable", "native-ui")
        import tomllib
        exported = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        plan = tomllib.loads((exported / "packages/nature.texts/1.0.0/manifest.toml").read_text())
        patch = plan["patch"][0]
        offset = patch["address"] - 0x80010000 + 0x800
        expected = bytes.fromhex(patch["expected"])
        replacement = bytes.fromhex(patch["replace"])
        assert expected == (base / "SLUS_011.15").read_bytes()[offset:offset + len(expected)]
        assert len(expected) == len(replacement) == 54
        assert replacement[:2] == b"\x0f\x00"  # P in the game glyph table
        run("--disable", "native-ui")
        clean = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        assert "patch" not in tomllib.loads((clean / "packages/nature.texts/1.0.0/manifest.toml").read_text())
        # Both formats share one installed list, resolved order and native patch path.
        v2_manifest = {"schema": 2, "id": "native-ui-v2", "name": "UI v2", "version": "1.0.0",
                       "game": "SLUS_011.15", "api": ">=1.0 <2",
                       "texts": {"messages.selectAMemoryCardSlot": "Pilih slot\nNatureLoader v2"}}
        archive = root / "native-ui-v2.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("mod.json", json.dumps(v2_manifest))
        run("--install", str(archive))
        assert run("--v2-status")["enabled"] == []
        run("--enable", "native-ui-v2")
        assert run("--v2-status")["loaded"] == ["native-ui-v2"]
        exported = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        patch = tomllib.loads((exported / "packages/nature.texts/1.0.0/manifest.toml").read_text())["patch"][0]
        assert len(bytes.fromhex(patch["replace"])) == 54
        assert bytes.fromhex(patch["replace"])[0:2] == b"\x0f\x00"
        # Legacy and schema 2 text conflicts use the same ordered resolver.
        run("--enable", "native-ui")
        assert run("--text", "messages.selectAMemoryCardSlot")["provider"] == "native-ui"
        run("--native-export", str(base / "SLUS_011.15"))
        run("--disable", "native-ui")
        assert run("--text", "messages.selectAMemoryCardSlot")["provider"] == "native-ui-v2"
        run("--disable", "native-ui-v2")
        clean = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        assert "patch" not in tomllib.loads((clean / "packages/nature.texts/1.0.0/manifest.toml").read_text())
        # Existing retail item fields are patched in guest RAM, not on the disc.
        balance_manifest = {
            "schema": 2, "id": "native-balance-v2", "name": "Balance", "version": "1.0.0",
            "game": "SLUS_011.15", "api": "^1.0",
            "content": {"$patch": {
                "base:turnip": {"sell_price": 120, "stamina": 6, "fatigue": -2},
                "base:turnip-seeds": {"buy_price": 80},
                "base:sickle": {"stamina": -1},
            }},
        }
        archive = root / "native-balance-v2.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("mod.json", json.dumps(balance_manifest))
        run("--install", str(archive))
        run("--enable", "native-balance-v2")
        status = run("--v2-status")
        assert status["nativeReady"] and status["nativeItemPatchCount"] == 5
        assert not status["contentGameplayReady"]  # new entities remain unavailable
        exported = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        patches = tomllib.loads((exported / "packages/nature.texts/1.0.0/manifest.toml").read_text())["patch"]
        by_address = {p["address"]: p for p in patches}
        values = {
            0x8004BBDE: (60, 120, 2),   # turnip sell price
            0x8004BBEC: (3, 6, 1),     # turnip stamina
            0x8004BBED: (-1, -2, 1),   # turnip fatigue
            0x8004B704: (120, 80, 2),   # turnip seed buy price
            0x8004B1EC: (-2, -1, 1),   # sickle stamina
        }
        assert len(patches) == len(values)
        original_exe = (base / "SLUS_011.15").read_bytes()
        for address, (stock, replacement, size) in values.items():
            patch = by_address[address]
            offset = address - 0x80010000 + 0x800
            assert bytes.fromhex(patch["expected"]) == original_exe[offset:offset + size]
            assert int.from_bytes(bytes.fromhex(patch["expected"]), "little", signed=True) == stock
            assert int.from_bytes(bytes.fromhex(patch["replace"]), "little", signed=True) == replacement
        # Supported item fields and UI text share one guarded snapshot.
        run("--enable", "native-ui-v2")
        mixed = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        assert len(tomllib.loads((mixed / "packages/nature.texts/1.0.0/manifest.toml").read_text())["patch"]) == 6
        run("--disable", "native-ui-v2")
        run("--disable", "native-balance-v2")
        clean = Path(run("--native-export", str(base / "SLUS_011.15"))["directory"])
        assert "patch" not in tomllib.loads((clean / "packages/nature.texts/1.0.0/manifest.toml").read_text())
        run("--import", str(rom), success=False)
        assert {path.name: digest(path) for path in base.iterdir()} == hashes_before
        assert not run("--status")["gameplayReady"]
    assert digest(rom) == original_hash, "Source ROM changed"
    print("PASS: real ROM import, native text/item-field exports, signed values, mixed mods, restoration, conflicts, restart persistence, unchanged base and ROM SHA-256.")


if __name__ == "__main__":
    main()
