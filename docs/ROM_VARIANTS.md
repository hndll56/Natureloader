# ROM variants and native compatibility

NatureLoader's ROM catalog stores original and prepatched BTN discs separately.
It identifies each image by its complete SHA-256, not its filename or the
`SLUS_011.15` boot filename. Import does not imply that a native runtime exists
for that image. The catalog always reports `nativeReady: false`; a separate
runtime compatibility gate must establish execution support.

## Local inspection results

The supplied `HM BTN IDN qol-ui-bin test-3.iso` is a **raw Mode 2/2352 disc**,
despite its `.iso` extension. Its supplied CUE describes that format correctly.
Both supplied discs have entry PC `0x80011278` and load address `0x80010000`,
but these matching values do not make their compiled code interchangeable.

| Property | Original USA | IDN QoL test-3 |
| --- | --- | --- |
| Disc size | 213954384 bytes | 214575312 bytes |
| Executable size | 323584 bytes | 735232 bytes |
| Executable payload size | `0x4E800` | `0xB3000` |
| Archive index size | 132 bytes | 136 bytes |
| Archive member count | 32 | 33 |
| Archive size | 133935104 bytes | 134064128 bytes |
| Overlay window A | `0x800B7E20` | `0x800C29B8` |
| Overlay window B | `0x8012DDE8` | `0x8013A1B8` |

The modified disc changes 199529 of the 227484 bytes in the original `.text`
address range. This comparison establishes that the stock compiled code cannot
be assumed compatible; it does not claim every differing byte is an intentional
logic change. Reordering/relocation also changes byte comparisons.

The farm member grows from 483328 to 489472 bytes. The title member grows from
57344 to 59392 bytes. The new member 32 occupies archive offsets 134053888 to
134064128 (10240 bytes). The bank of text at the stock address has a different
hash too. Stock function seeds, overlay load windows, text capacities and mod
addresses must not be applied blindly to this variant.

SHA-256 fingerprints measured from the supplied local files:

```text
Original disc:
41f81532a5fd70d6f79e7b5750513c35eee732d5e31ba144a1db7e90d84d97de
Original executable:
34e3b35dcd3fb1cd9a7d4813b8a9ef987915826e3d436f1908ecbe2d4fdf1444

IDN QoL test-3 disc:
af38445a6c8f896545b279f6333acdbb546abcbce1f89963c4dd073487a006f2
IDN QoL test-3 executable:
57f076d437a62feb50f6e8bb777a14d0fce7390bce1df11194676f7d0c7f1a9b
```

These fingerprints describe local inputs. They are not an independent release
authentication database or a claim that the patched game has been playtested.

## Catalog behavior

- Import BIN, ISO, IMG, a single-track MODE2/2352 CUE, or a ZIP containing exactly
  one BIN/ISO/IMG. Sector format is detected from the image, not the extension.
- Validate the executable header and address range, then read the complete
  archive index dynamically. Reject malformed, decreasing, unaligned, or
  out-of-bounds member ranges. The final offset must equal the archive size.
- Copy the disc into `roms/<full-sha256>/disc.bin` or `disc.iso` under the selected
  data directory. The user's source is opened read-only and never patched.
- Preserve the original extracted executable locally for diagnostics and native
  build input. A normalized CUE is generated for a raw-sector copy.
- Keep metadata in `rom.json` and per-ROM mod/profile data under `profile-data/`.
  `rom-selection.json` records the selected identity. Legacy `base/` is untouched.
- Deduplicate identical raw images and verify existing copies before reusing
  them. Raw BIN and cooked ISO representations have different raw hashes.
- Verify the full copied image and extracted executable before a launch gate
  accepts the selected revision. Checking only the executable is insufficient:
  overlays and assets can change independently.

The catalog is a storage and diagnostic component, not an emulator or an
automatic native port generator. A patched ROM needs a matching native build
and verified variant metadata. Save data and mod profiles must remain isolated
until compatibility between specific revisions has been established.

## Tests

`tests/rom_catalog_test.cpp` checks synthetic cooked and raw discs, a raw image
named `.iso`, 34-entry archive indexes, CUE/ZIP import, duplicate detection,
selection persistence, profile isolation, malformed input rejection, corruption
detection and source preservation. It accepts optional local ROM filenames as
arguments to repeat import and verification against real discs. Imported copies
live in a temporary test directory and are removed when the test exits.
