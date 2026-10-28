# Native runtime development audit

The local Linux launcher now offers an experimental native launch for the exact
verified USA disc. Title/menu boot and one in-game UI text mod are verified;
full farm gameplay is not yet verified. This audit uses the local
PSXRecomp research checkout in `reference/btn-recomp`; that runtime is not
linked into the Qt launcher or included in a distribution.

## Live retail item audit (2026-10-27)

`tools/audit_native_items.py` passed three separate native processes: stock,
`native-balance-v2` enabled, and disabled/restored. It waits for the actual
`game_state.itemInfo` pointer, reads five signed fields, checks advancing frames
and zero interpreted instructions, and verifies the source executable hash.

| Field | Stock | Enabled | Restored |
| --- | --- | --- | --- |
| Turnip sell price | 60 | 120 | 60 |
| Turnip stamina | 3 | 6 | 3 |
| Turnip fatigue | -1 | -2 | -1 |
| Turnip seed buy price | 120 | 80 | 120 |
| Sickle stamina | -2 | -1 | -2 |

Run from the project root, using your local supported disc:

```sh
python3 tools/audit_native_items.py \
  --launcher build/natureloader \
  --runtime reference/btn-recomp/build/Harvest_Moon__Back_to_Nature__Recompiled_ \
  --config reference/btn-recomp/game.toml \
  --disc /absolute/path/to/disc.cue \
  --exe local-data/base/SLUS_011.15
```

Requires Python 3.11+ and loopback sockets. It stops each process after collecting
evidence and removes private temporary profiles/saves. That intentional SIGTERM
can leave an `atexit` report; it is not evidence that boot failed.

The earlier temporary audit incorrectly reused a persistent debugger connection
and assumed `ping` supplied a frame counter. The server closes each connection
after one request; `frame` supplies frame counts. The reported early BIOS stop
from that audit was therefore inconclusive. The corrected audit reaches live
game data. It does not prove buying/using items, new items/NPCs, or farm save/load.

## New Game coverage work (2026-10-27)

A longer title/New Game probe found a native-only coverage gap at `0x800BC3AC`,
return address `0x800BBC64`. A 52-byte local live-RAM capture (CRC32 `DEE1DCFC`)
was compiled through the existing boot-variant dispatcher with live-byte guards.
After rebuilding, title/menu input and the mayor's New Game introduction worked.

Opening the name editor then stopped at `name_entry_step` (`0x800231E8`).
Its bytes matched the supported executable. An explicit function seed and main
code regeneration allowed the actual name editor to render and accept input.
Confirming a name subsequently exposed `name_sound_step` (`0x80023060`).

The address-only `tools/native-patches/runtime-entry-seeds.patch` includes 103
additional main-executable function entries absent from the previous seed list,
selected from the supplied decomp's `docs/retail-symbols.json`. Most were already
covered by discovery; the regenerated dispatch gained eight entries. Reference
metadata SHA-256: `958cb48b54ec6b9239a6e14cc9b9fde68668f4e8819d1fd776c6a4da55a8a3ca`.
The local native runtime rebuild succeeded. With those seeds, the probe
confirmed a player name, birthday, farm name, and dog name, and reached final
New Game confirmation. These are coverage improvements,
not evidence of complete farming, native save/load, or new item/NPC gameplay.

Confirming that setup exposed a farm-overlay coverage gap at `0x800E5A1C`,
return address `0x800E417C`, with zero interpreted instructions. Its 192-byte
routine has no branch targets outside the captured range. The local capture
(CRC32 `BCA41963`) compiled successfully and the eight-capture runtime rebuilt.
At that checkpoint only boot/menu were rechecked. The 2026-10-28 follow-up below
verifies passing this stop and reaching the rendered farm.

`tools/prepare_native_capture.py` reproduces capture-list preparation from
player-owned live RAM, verifies the recorded CRC/range, and preserves the input
capture list. See `tools/native-patches/README.md`. The checked-in metadata has
no game bytes. Generated captures, main/overlay C, screenshots, and binaries
remain local and are not part of the published launcher.

The C ABI now exposes `nm_item_fields` with a field presence mask, including
base patches, explicit zero values, and inheritance references. Core tests cover
new item/NPC handles across save, missing-mod, and restore cycles. Registry
validation rejects inheritance cycles and attributes patch failures to their
authors. These tests run against modcore, not the BTN inventory or NPC engine.

## Confirmed boot blocker

The native-only audit originally stopped at BIOS RAM vector `0x000000C0`.
After mapping the observed OpenBIOS A0/B0/C0 copies, execution reached a changed
game-code continuation at `0x8003EE04`. The neighboring code and game entry
still match the imported USA executable: this is not an empty executable load.
The RAM snapshot contains these four words starting at `0x8003EE04`:

```
00000000 00000000 10000009 00000000
```

The stock executable has different instructions. OpenBIOS contains GTE patch
callbacks; its compatibility patch path is the suspected producer. Executing
stock compiled code against these changed bytes would be incorrect. Restoring
the stock bytes or silently enabling the interpreter is not the chosen fix.

An offline recompilation of the captured 112-byte function at `0x8003EDE4`
produced two functions and eight exact dispatch identities. Its dispatch checks
the live code CRC before execution. This small native variant is linked beside
the original compiled game and overlays for the next boot audit. Runtime
instruction interpretation remains blocked by `NATURE_NATIVE_ONLY`.

## Overlay build

The original generated overlay translation unit was approximately 113 MB.
`tools/split_native_overlays.py` separates its existing function bodies into
103 translation units (15 banks, 50,196 functions), preserving the generated
code-validation dispatcher. The split runtime successfully linked on Linux.
These units currently use `-O0` for development coverage, not a performance
release build. The original generated monolith is preserved.

The splitter accepts the current emitter format only, refuses to overwrite an
output directory, and records the source SHA-256 in its manifest. Run its
focused tests with:

```
python3 -m unittest discover -s tests -p test_split_native_overlays.py
```

Generated C, RAM captures, ROM data and native game binaries remain local build
artifacts. They are not launcher source assets. Runtime adoption still requires
license review and packaging work; this research checkout is not a completed
portable backend.

## Required proof before enabling Play and in-game mods

- Reach the actual title/menu with rendered frames and working input under the
  native-only guard, then new game and farm gameplay.
- Resolve code variants by matching their bytes, without falling back to an
  instruction interpreter.
- Connect mod resources to the active game's UI/text/texture loading path.
- Verify one visible in-game UI change with the mod enabled, then restoration
  when disabled, while preserving the source ROM hash.

The existing content registry, texture viewer and PNG export do not satisfy
these gameplay gates. ROM hacks with changed executable code still require
native coverage for their own code variants.

## Subsequent boot checks

The first compiled variant passed the `0x8003EE04` stop; the next changed game
function was at `0x8003EC74`, resuming at `0x8003EC8C`. A second captured variant
was compiled. This exposed a recompiler CPS fall-through bug: an emitted block
ending at `0x8003EC98` checked interrupts but did not publish `0x8003EC9C` as the
next PC, causing an abnormal exit with PC zero.

The local recompiler now emits an explicit CPS continuation at such split
boundaries. Regenerating the variant passed that point and reached OpenBIOS
RAM `0x00002818`, with zero interpreted instructions. That kernel function has
three dynamically installed callback instructions at `0x281C..0x2824`, so its
stock native function correctly refuses the changed bytes. A third captured
variant is being tested. These are coverage advances, not a title-screen or
playability result.

The recompiler's MSVC/GCC CPS startup-hook generation check passes when run
from its framework directory. The Qt launcher suite still passes 5/5 tests,
including source-ROM preservation. The two focused overlay splitter tests pass.

## Verified result

Six native captures now cover the observed startup variants, the installed RAM
trampoline, and the interrupted stock entry continuation. A 30-second headless
run no longer entered the interpreter. A subsequent loopback debug audit
observed increasing frames, rendered the original Natsume/title screens, and
accepted Start/confirm input through the Continue menu to Memory Card selection.

`src/native_mods.cpp` translates active launcher text overrides into a separate
snapshot consumed by the native runtime. It guards the USA executable hash and
original bytes, encodes supported retail glyphs, enforces original string
capacity, and rejects active content mods without gameplay bindings. Runtime
mod directory selection uses `NATURE_MOD_DIRECTORY` in the local research
checkout. No disc sectors or source executable files are rewritten.

The `examples/native-ui` package changed the actual game's Memory Card prompt
to `Pilih slot` / `NatureLoader!`. Screenshot: `build/native-ui-confirm.png`.
The stock screenshot is `build/native-confirm.png`. This proves a bounded text
UI override, not arbitrary UI layout, HD sprite replacement, or new entities.

The Qt launch button verifies the entire imported USA disc, uses the catalog's
stored CUE/image, and isolates save/log paths per ROM. It rejects other disc
hashes, including the modified IDN image. The optional CMake variable
`NATURE_NATIVE_RUNTIME_DIRECTORY` selects the already-built local checkout.
The game runtime remains a research dependency, not a bundled release.

Disabling the example mod and exporting a fresh snapshot restored the original
Memory Card prompt in a second native run (`build/native-ui-restored.png`).
The integration suite verifies the base executable and original ROM hashes
remain unchanged after native text export and mod toggling.

## Native item model milestone, modcore only (2026-10-28)

**This is not native runtime evidence.** Claude's web sandbox had no `reference/btn-recomp`, no `local-data/`, no Qt, no
ROM/BIOS and no git checkout, so no BTN runtime, boot, farm, screenshot, RAM audit or retail save was exercised.

Implemented and tested in `modcore` only (`docs/NATIVE_ITEM_RUNTIME.md`): `ItemHandle`, vanilla id aliases,
`expanded:golden_turnip`, a headless inventory with acquire/select/use/buy, `mods/expanded.json` save with orphan
preservation, atomic writes and backups.

Results reported by that sandbox (`-DNATURE_BUILD_LAUNCHER=OFF`, GCC 13.3, CMake/Ninja, build dirs outside the tree):

| Run | Result |
| --- | --- |
| Baseline before changes (direct g++): modcore, boundary, base_patch, item_fields, script_boundary, fake_core | all pass |
| `ctest` Debug, 11 tests: `modcore`, `modcore_plugin_lifetime`, `modcore_boundaries`, `modcore_base_patches`, `modcore_item_fields`, `modcore_script_boundaries`, `modcore_native_items`, `modcore_native_items_example_load`, `modcore_end_to_end`, `modcore_examples_load`, `modcore_cli_errors` | 11/11 passed |
| `ctest` with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`, `ASAN_OPTIONS=detect_leaks=1` | 11/11 passed, no reports |
| `modcore_native_items`: 16 test groups | 0 failed checks (also under sanitizers) |
| Mutation checks of the new runtime (orphan slot reservation, unchanged-file skip, vanilla refusal, consume, preserved entries, affordability order) | each mutant failed at least one check |

Not run: Qt launcher tests (`core`, `modcore_library`, `mod_migration`, `rom_catalog`, `texture_decoder`,
`launcher_smoke`), `rom_integration`, and any `tools/*.py` audit.

Acceptance checklist status (first milestone):

- [x] Native item registry with stable handle: reuses `Registry`/`IdTable`; wrapped by `ItemHandle` (modcore).
- [x] Manifest registers `expanded:golden_turnip` (modcore).
- [x] Select, use, effect, buy, save, reload, disable, re-enable (modcore model, `ModelPlayer`).
- [ ] Item appears in the **native game** inventory: not done.
- [ ] Effect visible **in the game**, farm bootstrap, real save/load, native integration test: not done.
- [ ] Existing CTest incl. Qt targets: only the launcher-less subset ran.

## Local follow-up and farm coverage (2026-10-28)

The supplied `native-item-model.patch` applied after a clean patch check. Local
verification of the original patch passed 11/11 modcore tests, 11/11 ASan/UBSan
tests (`ASAN_OPTIONS=detect_leaks=0`), and 18/18 full tests, including Qt smoke
and player-owned ROM integration. No mutation-testing result was reproduced.

Local fixes add a seventeenth item-model test group: a broken save directory
must preserve live inventory, selection and the core save. Directory scan errors
now propagate; quarantine rejects invalid mod IDs. Windows target replacement
uses `MoveFileExW`, whose Windows execution is still unverified. Saves remain
non-transactional across files; save-version migration is still absent.

A state-aware New Game replay passed `0x800E5A1C` and exposed an indirect callback
at `0x800E2E98` (8 bytes, CRC32 `E38BCEA5`, return `0x800E28A0`). Its native capture
passed that stop and exposed `0x800E9778` (192 bytes, CRC32 `752E2871`, return
`0x800E417C`). The latter caller indexes the 113-entry event-command pointer
table at `0x800B975C`. Sixteen table entries were absent from the farm's compiled
dispatch. Metadata records those roots and a 37,812-byte code range beginning
`0x800E5704`, CRC32 `B7212388`. The range matched the stock extracted farm overlay
byte-for-byte. Its boundaries end at another known table function entry.

The local eleven-capture build generated 255 exact dispatch identities. A replay
with `tools/audit_native_newgame.py` passed character setup, entered scene 2,
rendered the farm/player/house, and advanced frames 4111 to 4929 over a 15-second
observation with zero interpreted instructions. Evidence is local at
`build/newgame-audit-20261028-handlers/`, including `result.json`, screenshots,
logs and private saves. The source executable hash remained unchanged.

Repeat using a fresh evidence directory:

```sh
python3 tools/audit_native_newgame.py \
  --runtime reference/btn-recomp/build/Harvest_Moon__Back_to_Nature__Recompiled_ \
  --config reference/btn-recomp/game.toml \
  --disc /absolute/path/to/disc.cue \
  --exe local-data/base/SLUS_011.15 \
  --evidence-dir build/newgame-audit-new
```

The probe guards the executable hash and uses private saves/empty mods. It reads
actual state to distinguish title intro, title menu, name input, birthday and
final confirmation. Final confirmation may use menu phase 7; its title state is
the authoritative gate. Add `--movement` to
require live player position to change after a Right input. Success is explicitly
coverage evidence, not a full farming/save certification.

The live retail item audit also passed stock/modded/restored runs on this build:
frames 637/642/633, zero interpreted instructions, correct five field values,
unchanged source executable. New mod inventory, item rendering/use, `PlayerPort`
integration, shop call sites, and real gameplay save/reload remain unimplemented.

Player layout research: a MIPS32 record-layout check of the reference headers
places `FarmState.player` at offset `0x51C8` from `game_state` (`0x8006C838`).
Stamina/max stamina/fatigue are signed 16-bit at player offsets `0x12/0x14/0x16`;
gold is signed 32-bit at `0x5C`. Calendar day is signed 16-bit at `0x80070CF4`,
zero-based 0..29. A real New Game RAM capture read 100/100/0 stamina/max/fatigue,
500 gold, year/season/day 0/0/1. The newer decomp documents stamina bounded by
max stamina (0..255), fatigue 0..100, gold 0..9,999,999. This is layout evidence,
not a working `PlayerPort` or permission to write arbitrary guest RAM.

Final regression on the twelve-capture build passed retail stock/mod/restored
RAM checks at frames 631/626/623. The final state-aware New Game probe
(`build/newgame-audit-20261028-final/result.json`) passed a 30-second observation
from frames 3719 to 5017 with zero interpreted instructions and an unchanged
source executable. Its `farm_gameplay_verified` remains false by design.

A longer replay subsequently reached an uncompiled installed OpenBIOS RAM
continuation at physical `0x00002814`, return `0x000027EC`. Its KSEG0 alias
`0x80002814` now has an audited 348-byte capture, CRC32 `316E400F`. The resulting
twelve-capture build contains 265 exact dispatch identities. It passed setup,
30 seconds of farm observation, and actual mayor dialogue/childhood flashback
progression without another observed coverage stop. The initial movement check
was made during this scripted opening (`player.stateFlags = 0x04800000`, including
`PLAYER_FLAG_SCRIPTED`); it does not prove a movement defect. The extended
`--movement` probe advances dialogue but timed out after 180 seconds waiting for
the opening event to release control. Evidence:
`build/newgame-audit-20261028-kernel/` and `build/newgame-audit-20261028-intro/`.
Free movement, the end of the opening, farming, day advance and retail save/load
are therefore still unverified. Do not mark the complete hybrid item milestone
done. Short observation success must not be reported as a completed gameplay test.

The shipped `hello-text` example now uses `Ya\nTidak`: eight supported glyphs.
The previous longer prompt exceeded retail capacity; the intermediate slash
spelling also used an unsupported glyph. Launcher regression tests install the
actual example and require native preflight success.
