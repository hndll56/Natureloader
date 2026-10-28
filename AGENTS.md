# NatureLoader agent contract

Read this file with `README.md`, `docs/PORTING.md`, and `docs/NATIVE_PROGRESS.md` before changing the project.

## Rules

- Report status honestly. Distinguish authoring, native export, and playable runtime.
- Never commit ROMs, ISOs, game executables, saves, extracted game data, research runtimes, or build output.
- Never claim a feature is playable without repeatable runtime evidence.
- Preserve the Windows/Linux launcher. Android is a later target.
- Modcore is the unified loader's internal engine, not a separate workflow or tab.
- Use one mod storage model: `mods/`, `profile.json`, resolver, and native export.
- Migration must be idempotent, atomic, and backed up.
- Never write to the source disc. Native patches use a runtime copy.
- Every feature needs validation, tests, documentation, and a completion gate.

## Code map

- `src/core.*`: launcher facade, schema 1/2 packages, profiles, CLI.
- `src/modcore_library.*`: unified mod lifecycle and registry.
- `src/mod_migration.*`: migration from old profiles and `mods-v2/`.
- `src/native_mods.*`: native plan/export; currently text and retail item fields.
- `src/rom_catalog.*`: import, hashing, identification, per-ROM profiles.
- `src/rom_panel.*`, `src/main.cpp`: launcher UI.
- `modcore/`: resolver, registry, VFS, hooks, scripts, saves, plugins, C API.
- `modcore/vanilla_ids.*`, `native_items.*`, `native_save.*`: headless native item model (see `docs/NATIVE_ITEM_RUNTIME.md`). Modcore only; not connected to the BTN runtime.
- `tests/`: unit, integration, boundary, migration, and smoke tests.
- `examples/`: safe mod authoring and export packages.
- `docs/`: public contracts, runtime evidence, limits, and milestones.

## Workflow

1. Read this file and check `git status`.
2. Reuse existing APIs before adding new ones.
3. Define a small slice and testable completion gate.
4. Validate before mutating data or calling runtime code.
5. Add regression tests and document evidence and limits.
6. Run relevant builds and tests.
7. Update this file, `docs/AI_AGENT_GUIDE.md`, and README when public behavior changes.
8. Review files before commit. Exclude all game data.
9. Commit and push `main` through the GitHub connector when shell networking is unavailable.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF
cmake --build build-modcore --parallel
```

The local native runtime uses `NATURE_NATIVE_RUNTIME_DIRECTORY`; never publish it.

## Priorities

Connect the registry to runtime; add a sandboxed hook/script VM; add entity allocation; connect assets, maps, NPCs, events, collision, and save/load; prove a native farm slice; then expand sprites, items, tools, buildings, maps, and packaging.

Currently proven native features are selected UI text and retail item field patches for USA. New entities, gameplay sprites, areas, hooks, scripts, plugins, and native saves remain unconnected.

`tools/audit_native_items.py` now verifies stock/modded/restored values in live native RAM, advancing frames, zero interpreted instructions, and unchanged source executable. The debugger requires a fresh connection per request; its `ping` fast path does not return a frame counter. Use `frame` instead. Do not misclassify intentional audit cleanup as a BIOS crash.

For item table integration use `nm_item_fields` and its presence mask: apply only explicitly authored fields after copying an inherited row. Legacy `nm_item_row` supplies zero defaults and cannot distinguish an absent field from an explicit zero. This API addition alone does not allocate native tables or spawn entities.

Registry validation rejects item inheritance cycles, including cycles introduced by patches; diagnostics identify the field author. `tools/native-patches/runtime-entry-seeds.patch` records additional main EXE function metadata from the decomp. Runtime coverage progress and unresolved stops belong in `docs/NATIVE_PROGRESS.md`.

`tools/prepare_native_capture.py` appends only CRC-verified ranges from a local RAM dump and refuses output overwrites. Its metadata is in `tools/native-patches/coverage_entries.json`; audited ranges may specify multiple function entries. The New Game gaps at `0x800E5A1C`, `0x800E2E98` and `0x800E9778` are passed by the local coverage build. Sixteen entries from the 113-slot farm event-handler table were missing from static discovery. Their code range matches the stock overlay. `tools/audit_native_newgame.py` reproduces setup and records a rendered farm with advancing frames and zero interpretation; this does not certify farming, day advance, retail save/load, or new mod entities.

Native item model milestone (modcore only): `expanded:golden_turnip` is registered, acquired, bought, selected, used, saved to `mods/expanded.json`, reloaded, and survives disable/re-enable in `tests/modcore/native_item_tests.cpp`. It runs against `ModelPlayer`, a test double. It does **not** show an item in the native game inventory, use it on the farm, or touch a retail save; those need `reference/btn-recomp` and `local-data/`. Canonical retail ids stay `base:<name>`; `vanilla:` spellings are native-layer aliases only (`modcore/vanilla_ids.h`). Do not add a second registry.

Native save-directory scan failures must fail without clearing live inventory or writing core state. Validate mod IDs before quarantine operations. Windows replacement uses `MoveFileExW`; Windows runtime testing remains required. Save operations are not a transaction across core and per-mod files.

The twelve-capture local build also includes installed OpenBIOS continuation `0x80002814`. Longer probes show mayor dialogue and childhood flashback progression. The scripted opening has not yet returned player control within the 180-second movement probe, so free movement and the full farm slice remain unverified. Keep this failure visible; do not bypass script/input flags to manufacture a movement result.

## GitHub handoff

Push to `hndll56/Natureloader`, branch `main`. Verify remote head and tree contents. Ensure no ROM or binary artifacts are included. Any architecture change must update this file and `docs/AI_AGENT_GUIDE.md` in the same commit.
