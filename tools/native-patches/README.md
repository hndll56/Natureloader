# Local native research changes

These patches record authored changes to the research checkouts used by this
workspace. They contain no ROM bytes or generated game C.

- `framework.patch`: OpenBIOS RAM vector mappings, interpreter rejection and
  coverage diagnostics, CPS split fall-through fix, per-launch mod directory.
- `runtime-build.patch`: native-only definition and split-overlay build inputs.

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
