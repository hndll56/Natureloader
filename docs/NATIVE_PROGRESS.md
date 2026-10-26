# Native runtime development audit

The local Linux launcher now offers an experimental native launch for the exact
verified USA disc. Title/menu boot and one in-game UI text mod are verified;
full farm gameplay is not yet verified. This audit uses the local
PSXRecomp research checkout in `reference/btn-recomp`; that runtime is not
linked into the Qt launcher or included in a distribution.

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
