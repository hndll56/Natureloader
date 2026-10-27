# Porting plan and observed status

## Scope

The intended product is a native PC game runtime with an integrated mod
launcher. Players supply their own disc once, then install and switch mods
without replacing the disc image. Windows and Linux are first; Android follows.
Version 0.3 also includes the experimental standalone modcore library/CLI.
The Qt launcher now routes schema 1 and 2 through one package/profile backend
using modcore for ordering and validation. Native export supports text and four
existing retail item fields; new entities and gameplay hooks remain unbound.
See `NATIVE_ITEM_MODS.md` and `MOD_MIGRATION.md` for the current boundaries.

## Findings from the supplied decompilation

The supplied `backtonature-decompiled-main.zip` describes a matching rebuild
of the main USA executable using PSY-Q 4.6 and GCC 2.95. Its README reports
461 matched C functions. That matching claim has not been independently
reproduced here: the matching SDK/container build was not run.

That earlier archive explicitly excludes decompiled code overlays in
`A_FILE.BIN` and keeps linked PSY-Q runtime code as assembly generated from the
ROM. A second, newer user-supplied archive,
`hmbtn-decomp-main.zip`, is a separate CC0-licensed research source. Its
upstream identity has not been established from the archive alone. The
[beruanglaut-com project](https://github.com/beruanglaut-com/backtonature-decompiled)
is credited separately for the earlier Unlicense reference.
Its README describes a matching rebuild of the EXE and 13 overlays; its source
tree includes farm (`11_GF_FARM`), title (`27_GF_TITLE`), save/load
(`15_saveload`), memory card, staff roll, and minigame overlays. This is a
major research reference for gameplay porting. We have inspected the archive
without extracting or copying its source into NatureLoader.

The source still targets PS1, MIPS, and PSY-Q. A matching rebuild proves
compatibility with the original binary, not compatibility with a PC runtime.
Overlay entry points, fixed memory addresses, PSY-Q calls, GTE graphics, CD
access, and audio need host-side implementations or adapters. The new archive
may make farm logic available to port, but does not by itself make farming
playable in NatureLoader. The older supplied archive's main-loop evidence is:

| Area | Local source evidence | Implication |
| --- | --- | --- |
| Title | `src/runtime/main.c`, `SCENE_TITLE`, `titleStepDispatch` | A PC main loop cannot call the retail MIPS entry point directly. |
| Farm | `src/runtime/main.c`, `SCENE_FARM`, `farmEnterDispatch` | The older reference does not include farm overlay source; the newer archive includes it, but its PS1 dependencies still need porting. |
| Overlay selection | `include/game/nudge_axis.h`, `AFILE_FARM = 11`, `AFILE_TITLE = 27` | Locate and analyze these members first. |
| Memory | `src/runtime/main.c`, fixed `0x801BE1B0` memory and integer pointers | Host pointers and ownership require deliberate redesign, especially on x64. |
| Textures | `src/graphics/archive_3.c`, `DecodeContainer0D`, MIPS register constraints, `LoadImage` | Need a portable decoder and native texture upload backend. |
| Geometry | `src/geometry/vec_length.c`, GTE operations | Need compatible math before changing precision or scale. |
| Disc I/O | `src/cd/archive.c`, `afile_init` and CD reads | Replace sector/device access with host asset access. |

Paths in this table are relative to `reference/backtonature-decompiled-main/`.
The original ZIPs and extracted reference remain unchanged.

The local import successfully identified 32 archive members. Member 11 (farm)
starts at archive offset 121489408 and spans 483328 bytes. Member 27 (title)
starts at 133756928 and spans 57344 bytes. These are archive spans, including
possible padding/data; they are not claims about exact function-code sizes.

## Implemented boundary

```text
User disc (read only)
    Native ISO reader + executable/text-bank verification
    Local base data + archive index + decoded text bank
                                      |
Text mod ZIP -> validated manifest -> ordered text resolver
                                      |
                              Qt native text viewer
```

No emulator, runtime code execution, PSY-Q SDK, BIOS, or ROM is bundled.
The native viewer proves the host-side asset/mod path and Unicode rendering.
It does not execute the decompiled gameplay or original dialog interpreter.

Qt Widgets supplies the desktop shell. The reusable import/mod operations are
in `src/core.*` and contain no widgets. libarchive handles ZIP parsing instead
of a custom archive implementation. Qt Core is currently reached through the
Widgets CMake dependency; it can be split when a second host actually needs it.

## Next milestones and completion gates

1. **Title overlay and asset formats.** Identify the title overlay's entry
   points, globals, relocations and imported functions. Decode its actual
   textures from the supplied disc. Gate: produce the original title screen
   in a host window with working menu input and documented comparisons.
   A static image or replay is insufficient evidence of a working game port.
2. **Native platform backend.** Provide graphics, GTE math, audio, timing,
   controller input, file access, and save interfaces required by the title
   path. Keep retail layout parsing separate from host-owned memory. Evaluate
   existing compatibility code before writing a broad PSY-Q replacement.
3. **Playable farm slice.** Port required routines from the now-available farm
   overlay source and implement its missing host dependencies.
   Gate: new game, character movement, map transition, one farming action,
   sleep/day advance, and save/reload produce repeatable results.
4. **Runtime asset mods.** Connect stable logical asset IDs to file overrides
   at the asset-loading boundary. Gate: switch one real character sprite in
   gameplay, disable it, and restore the original without changing base data.
   HD artwork also needs metadata for scale, pivots, animation and collisions.
5. **Content extension.** Replace fixed tables and numeric assumptions with
   registries for items, NPCs, maps, buildings and tools. Define save ownership
   and missing-mod behavior before expanding content. Gate: add one entity
   beyond the retail count, save, reload, and resolve its ID deterministically.
6. **Player distribution.** Test native Windows and Linux packages on clean
   systems, include required runtime dependencies and notices, and verify no
   imported game data appears in releases. Android then needs storage access,
   touch/controller UI, packaging, and performance testing on real hardware.

These are gates, not promises that the features already exist. There is no
credible schedule for the full port until the overlay work has been measured.
Removing PS1 limits is a subsystem-by-subsystem change, not a build flag;
the PC still has resource limits and mods still need compatibility rules.

## Reuse candidate

[PsyZ](https://github.com/Xeeynamo/psyz) is an existing PlayStation porting
framework whose documented goal is running reconstructed game code as native
applications. It is a candidate to evaluate for the platform backend. It is
not integrated here and does not provide the missing BTN overlay source.
An adoption decision requires checking the APIs BTN uses and the licensing
and behavior of the relevant backend, rather than assuming drop-in support.

## Verification and remaining uncertainty

- CTest covers synthetic ISO/BIN reads, cross-sector access, malformed data,
  text decoding, mod validation, conflicts, ordering, and persisted profiles.
- An offscreen smoke test constructs and paints the real Qt launcher.
- Optional integration tests import the supplied ROM, resolve text through
  enabled mods, restart the process between operations, and compare original
  ROM/base hashes after disabling the mods.
- Linux build and offscreen rendering have been exercised. Windows and Android
  builds, actual gameplay, sound, controller input and game saves have not.
- Full disc authenticity is not inferred from the executable hash. Archive
  hashes are recorded for diagnostics, not represented as independently
  verified release fingerprints.
