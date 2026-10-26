# Third-party material

## `beruanglaut-com/backtonature-decompiled`

Source repository: [backtonature-decompiled](https://github.com/beruanglaut-com/backtonature-decompiled).
The user supplied `hmbtn-decomp-main.zip`, an archive of this project, for
research and port development. The project describes a matching decompilation
of the USA PlayStation release (`SLUS_011.15`). Its `LICENSE` applies the
Unlicense to that repository's original reconstruction, tools, metadata, and
documentation. This credit records the source used; it does not imply that
NatureLoader contains copied game code or that the game itself is licensed
under the Unlicense.

The upstream [`NOTICE.md`](https://github.com/beruanglaut-com/backtonature-decompiled/blob/main/NOTICE.md)
also credits `coalslav/hmbtn-decomp` (CC0 1.0) and the BTNx translation project
for parts of its reconstruction and text-codec knowledge. See upstream notice
for details.

## User-supplied matching decompilation archive

Source: `backtonature-decompiled-main.zip`, supplied locally by the user.
Its `LICENSE` declares the original reconstruction, tooling, metadata and
documentation under the Unlicense. A copy is in `licenses/decomp-Unlicense.txt`.

`resources/text_bank.layout.json` is copied unchanged from its
`src/text/text_bank.layout.json`. `decodeText` in `src/core.cpp` follows the
glyph mapping in its `tools/text_codec.py`; archive-index interpretation follows
`src/cd/archive.c`. These contain metadata/mappings, not extracted game text. The native texture
decoder in `src/texture_decoder.cpp` follows the supplied reconstruction of
the game's container 0D and byte-RLE algorithms. Decoded images come only from
the user's imported data and are not bundled.

The supplied project's `NOTICE.md` credits
[coalslav/hmbtn-decomp](https://github.com/coalslav/hmbtn-decomp) (CC0 1.0) for
parts of its reconstruction and the BTNx translation project for font-atlas
and text-codec knowledge. The full upstream notice is preserved locally in
`reference/backtonature-decompiled-main/NOTICE.md` and copied to
`licenses/decomp-NOTICE.md` for provenance.

The upstream notice explicitly excludes the game and Sony PSY-Q SDK from its
license grant. NatureLoader does not bundle those files or grant rights in
them. Importing a user's ROM is a technical design choice, not a statement
that all uses or distributions are free of copyright obligations.

## Runtime dependencies

Qt 6 and libarchive are external dependencies linked by the build. They retain
their respective licenses. A distributed application package must include
the notices and comply with the license terms of the actual dependency builds
shipped. This source prototype does not include third-party binary bundles.

## Optional local native research runtime

The development configuration can launch the separate local
`reference/btn-recomp` checkout, based on
`vinibeloni/harvest-moon-btn-psx-recomp` commit
`e3bbd24f26b4a762b8c9d8aa901c761e0887cb59`, with PSXRecomp commit
`fce6eba1f2be16ce705b09e6efc3eec3d33ad8b4`. The framework's LICENSE declares
PolyForm Noncommercial License 1.0.0. Its bundled OpenBIOS carries its own MIT
notice. Their complete license files remain in that checkout; neither runtime
nor generated game code is included by this project's install target.
Local source changes are recorded in `tools/native-patches/`. This experimental
integration is not a relicensing of the framework or the game.
