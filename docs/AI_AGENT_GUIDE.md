# NatureLoader: AI agent handoff guide

Use this guide to understand, modify, test, and continue NatureLoader without repeating old investigations or making unsupported claims.

## Product

NatureLoader is a desktop launcher and native-port foundation for Harvest Moon: Back to Nature. Players provide their own disc or ROM. The launcher imports and hashes it read-only, keeps a profile per ROM, and enables mods without changing the source disc.

This is not an emulator or a complete remake. The native runtime is incremental. Modcore is an internal library used by the unified launcher.

## Architecture

```text
Player ROM (read-only) -> catalog/hash/profile
mods/<id>/ + profile.json -> ModcoreLibrary resolver
resolver -> native plan, content preview, future runtime hooks/assets/save
native runtime copy
```

Schema 1 and schema 2 share the installer, folders, profile, load order, conflicts, text viewer, and native exporter. `--v2-*` commands are compatibility aliases.

## Current boundary

- Native export: selected UI text and `buy_price`, `sell_price`, `stamina`, `fatigue` for 380 USA retail base items.
- Authoring registry: items, NPCs, locations, buildings, tools, assets, hooks, scripts, and metadata can be validated and displayed.
- Not playable: new entities, HD gameplay sprites, new areas/buildings/tools/events, Lua, gameplay plugins, and native save extensions.
- Boot/title/Memory Card menu were tested on the research runtime. Farm gameplay, save/reload, Windows, and Android are not release evidence.

Unconnected content must fail closed: registry storage is allowed, but native export must reject it with a clear error until a runtime consumer exists.

## Development rules

Use C++17, CMake, Qt 6 Widgets, libarchive, and existing modcore dependencies. Preserve public API names and compatibility wrappers. Validate before mutation. Use staging, atomic rename, limits, and backups for profiles/files.

Every native patch must verify executable/hash, load-image bounds, type/range/capacity/conflicts, and patch a runtime copy. The source disc must remain byte-identical.

For new content, use this order: logical ID/schema; registry allocation; asset/VFS lookup; runtime consumer/hook; save ownership; add/load/save/reload/disable tests; documentation and compatibility version.

## Validation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use `NATURE_TEST_ROM` only with a local untracked path. Use the Qt-off build for modcore changes and run `launcher_smoke` for launcher changes. Record unavailable tests and reasons.

## Required documentation

Update the relevant public docs when behavior changes: `docs/MOD_API.md`, `docs/MEMBUAT_MOD.md`, `docs/MOD_CONTENT.md`, `docs/NATIVE_ITEM_MODS.md`, `docs/NATIVE_PROGRESS.md`, `docs/PORTING.md`, `docs/DECOMP_INTEGRATION.md`, `docs/MOD_MIGRATION.md`, `AGENTS.md`, and this guide.

Document test evidence, target ROM/runtime, and limits. Never change “preview” to “playable” without runtime proof.

## Handoff

Before push, run `git status --short` and `git diff --check`. Commit only source, tests, docs, examples, licenses, and build configuration. Exclude ROM/ISO/BIN/EXE/DLL/SO, saves, `reference/`, `local-data/`, and build output. Push `main`, verify the remote commit/tree, and report the commit SHA, tests, and limits.

## Next milestone

Prove a native farm slice: new game, movement, map transition, one farming action, sleep/day advance, and save/reload. Then connect one gameplay sprite override and one new entity with deterministic save IDs.
