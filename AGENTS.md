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

`tools/prepare_native_capture.py` appends only CRC-verified ranges from a local RAM dump and refuses output overwrites. Its metadata is in `tools/native-patches/coverage_entries.json`. Last observed farm transition gap: `0x800E5A1C` after final New Game confirmation. Its capture compiles, but that transition is not yet verified after the rebuild. New item/NPC acceptance remains unfinished.

## GitHub handoff

Push to `hndll56/Natureloader`, branch `main`. Verify remote head and tree contents. Ensure no ROM or binary artifacts are included. Any architecture change must update this file and `docs/AI_AGENT_GUIDE.md` in the same commit.
