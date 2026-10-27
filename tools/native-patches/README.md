# Local native research changes

These patches record authored changes to the research checkouts used by this
workspace. They contain no ROM bytes or generated game C.

- `framework.patch`: OpenBIOS RAM vector mappings, interpreter rejection and
  coverage diagnostics, CPS split fall-through fix, per-launch mod directory.
- `runtime-build.patch`: native-only definition and split-overlay build inputs.
- `runtime-entry-seeds.patch`: 103 additional USA main-executable function
  addresses from the decomp's `docs/retail-symbols.json`, including the observed
  New Game failures at `name_entry_step` (`0x800231E8`) and `name_sound_step`
  (`0x80023060`). Some addresses are already covered by automatic discovery.
  Regenerate main code after applying this patch. It contains metadata only.

Baselines are recorded in `THIRD_PARTY_NOTICES.md`. Apply only to matching
checkouts after checking local changes. These patches are not a complete build
bootstrap: the existing generated main/BIOS C, audited native boot captures,
and supplied disc are separate local inputs.

`../split_native_overlays.py` produces `native_overlay_parts` from the existing
generated monolith. The development dispatcher additionally calls
`nature_boot_dispatch` before its normal dispatch. That function is generated
from `reference/btn-recomp/build/native-boot-variant.json`, with per-function
live-byte CRC checks, and compiled with renamed dispatcher/statistics exports.
The file is a local RAM capture containing game/BIOS code, not a distributable
source asset. Do not interpret an unhandled address to bypass native coverage.

See `docs/NATIVE_PROGRESS.md` at the project root for audit evidence and limits.

The 2026-10-27 local coverage build additionally captures the title routine at
`0x800BC3AC` (52 bytes, CRC32 `DEE1DCFC`) in the existing native boot-variant
capture list. Its first observed caller returned to `0x800BBC64`. Generate this
capture from the player's live USA RAM; do not distribute the captured bytes.
Compile it with the existing `compile_overlays.py --static` pipeline and link
through `nature_boot_dispatch`. This is local research coverage, not a bundled
or independently reproducible full runtime. The main entry seed above needs
the supported executable and a compatible recompiler.

The farm-overlay entry at `0x800E5A1C` was observed immediately after the final
New Game confirmation. Its audited range is 192 bytes, CRC32 `BCA41963`, with
return address `0x800E417C`. `coverage_entries.json` records both ranges without
including game bytes. Use the checked-in tool to append a verified capture:

```sh
python3 tools/prepare_native_capture.py \
  --ram /absolute/path/to/local-ram-dump.bin \
  --captures reference/btn-recomp/build/native-boot-variant.json \
  --entry 0x800E5A1C \
  --output build/native-boot-next.json
```

It requires exactly 2 MiB of RAM, checks range alignment and CRC, preserves
existing captures, and refuses to overwrite the output file. Compile the new
capture list through the research runtime's `compile_overlays.py --static`
workflow. The generated JSON contains game code and must stay local.
