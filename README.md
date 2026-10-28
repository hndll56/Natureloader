# NatureLoader

Desktop launcher and native-port foundation for Harvest Moon: Back to Nature.

**Status: experimental.** The launcher and mod catalog work on Linux. The local research runtime reaches a rendered farm after New Game under the native-only guard. Full farming, day advance, save/reload, Windows, Android, and arbitrary ROM hacks are not verified.

## Goals

- Let players manage their own discs and ROM hacks as separate entries.
- Install, enable, disable, reorder, and remove modular ZIP mods without changing the source disc.
- Build a native Windows/Linux port, then target Android.
- Support large community content through extensible registries and runtime APIs.

NatureLoader is not an emulator and does not rebuild the entire game from scratch. The native port uses reconstructed game code and a PC backend. PS1 overlays and platform dependencies still require staged porting.

## Architecture

```text
Player disc/ROM (read-only)
  -> catalog, hash, profile, ISO/archive reader
  -> ZIP mod validation -> resolver and load order
      -> text/native plan
      -> content registry preview
      -> future runtime hooks/assets/save
  -> native runtime copy
```

The launcher uses C++17, Qt 6 Widgets, and libarchive. Core logic lives in `src/core.*`, `src/rom_catalog.*`, `src/modcore_library.*`, and `src/native_mods.*`. The optional research runtime is local and never bundled.

## Current mod support

Schema 1 and schema 2 use one **Mod Chest** workflow, one `mods/` directory, one `profile.json`, and one native export path. Modcore is the internal resolver/registry engine. `--v2-*` commands remain compatibility aliases.

Native export currently supports selected UI text and these retail item fields: `buy_price`, `sell_price`, `stamina`, and `fatigue`. Content registry entries for items, NPCs, locations, buildings, tools, assets, hooks, and scripts are authoring/preview data until connected to gameplay.

The live native audit verifies stock/modded/restored item RAM, advancing frames,
zero interpreted instructions, and an unchanged executable. See
[native audit instructions](docs/NATIVE_PROGRESS.md). New-item adapters can use
the C ABI's `nm_item_fields` presence mask to preserve inherited fields.

Not yet playable: new items or entities, HD gameplay sprites, new maps, buildings, tools, events, Lua, gameplay plugins, and native save extensions.

The [native item model](docs/NATIVE_ITEM_RUNTIME.md) adds a modcore inventory,
item use/purchase through `PlayerPort`, and per-mod save files. Its tests use
`ModelPlayer`; it is not yet connected to the game's inventory or player.

## Build on Linux

Requirements: C++17 compiler, CMake 3.21+, Qt 6 Widgets, and libarchive development headers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/natureloader --data local-data
```

Modcore-only build:

```sh
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF
cmake --build build-modcore --parallel
./build-modcore/naturemod check examples/v2
```

## Native runtime

The runtime is not bundled. Configure a compatible local checkout:

```sh
cmake -S . -B build -DNATURE_NATIVE_RUNTIME_DIRECTORY="$PWD/reference/btn-recomp"
cmake --build build --parallel
```

Import a player-owned supported USA disc in the ROM collection, then use **Run native (test)**. The executable hash is checked before launch. ROM hacks with different executables are cataloged but may not run natively.

## Mod examples

Minimal schema 1 text mod:

```json
{
  "schema": 1,
  "id": "my-ui-mod",
  "name": "My UI Mod",
  "version": "1.0.0",
  "game": "SLUS_011.15",
  "texts": { "messages.selectAMemoryCardSlot": "Choose a memory card" }
}
```

Package it as a ZIP with `mod.json` at the root. Install it from the launcher, enable it, and inspect the resolved status. See [Making mods](docs/MEMBUAT_MOD.md), [Mod API](docs/MOD_API.md), and [Content registry](docs/MOD_CONTENT.md).

## Documentation

- [Documentation index](https://github.com/hndll56/Natureloader/blob/main/docs/README.md)
- [AI agent guide](https://github.com/hndll56/Natureloader/blob/main/docs/AI_AGENT_GUIDE.md)
- [Agent contract](https://github.com/hndll56/Natureloader/blob/main/AGENTS.md)
- [Making mods](https://github.com/hndll56/Natureloader/blob/main/docs/MEMBUAT_MOD.md)
- [Mod API](https://github.com/hndll56/Natureloader/blob/main/docs/MOD_API.md)
- [Content registry](https://github.com/hndll56/Natureloader/blob/main/docs/MOD_CONTENT.md)
- [Native item patches](https://github.com/hndll56/Natureloader/blob/main/docs/NATIVE_ITEM_MODS.md)
- [Migration](https://github.com/hndll56/Natureloader/blob/main/docs/MOD_MIGRATION.md)
- [Architecture](https://github.com/hndll56/Natureloader/blob/main/docs/ARCHITECTURE_V2.md)
- [Porting plan](https://github.com/hndll56/Natureloader/blob/main/docs/PORTING.md)
- [Native status](https://github.com/hndll56/Natureloader/blob/main/docs/NATIVE_PROGRESS.md)
- [ROM variants](https://github.com/hndll56/Natureloader/blob/main/docs/ROM_VARIANTS.md)
- [Third-party notices](https://github.com/hndll56/Natureloader/blob/main/THIRD_PARTY_NOTICES.md)

## Privacy and licensing

Never publish ROMs, game executables, extracted files, music, images, or private saves. Decompilation licenses do not grant rights to the original game. Review [third-party notices](THIRD_PARTY_NOTICES.md) before redistribution.

## Contributing

Keep changes reproducible. Before adding a content type, define its schema, validation, save compatibility, runtime consumer, and completion test. Do not claim content is playable before runtime verification.
