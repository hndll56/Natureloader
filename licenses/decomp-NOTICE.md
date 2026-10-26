# Notice: credits, scope and third-party material

## What the license covers

`LICENSE` (the Unlicense) applies only to the original work in this
repository: the C reconstruction in `src/`, the type and declaration headers
written for it, the build and matching tools, the configuration, and the
documentation.

It does **not** grant any right in *Harvest Moon: Back to Nature* itself or in
the Sony PSY-Q SDK. Copyright in the game, including its program, text,
graphics, audio and other data, belongs to Marvelous Inc. and its licensors.
The game is published in North America by Natsume Inc. None of those files is
included here. Every game-derived file the build needs (`baserom/`, the split
`asm/`, `src/text/text_bank.json`) is generated locally from the user's own
copy and is gitignored.

## coalslav/hmbtn-decomp

<https://github.com/coalslav/hmbtn-decomp>, released under CC0 1.0.

The following was taken from that project and adapted to this project's
compiler, naming and translation-unit layout:

- **Function implementations (24)**, ported and then matched against the
  retail instructions:
  - `main`
  - `anim_tick`, `sprite_draw_frame`, `text_sprite_draw`
  - `text_draw_centered`, `text_draw_right`, `text_printf`,
    `text_window_frame`, `text_draw_string`, `text_glyph_sprite`,
    `text_draw_face`, `text_insert_source`
  - `model_draw_tmd_object`, `model_relocate_textures`
  - `ground_find_object`, `ground_primitive_under`, `ground_triangle_under`,
    `ground_quad_under`
  - `vec_length_bounded`
  - `pad_set_vibration`
  - `sound_load_step`
  - `shop_restock`
  - `menu_box_animate`, `name_entry_key_input`

  Most of these carry a `Ported from hmbtn-decomp (CC0)` comment at the
  definition.
- **Type definitions**: structure layouts from hmbtn-decomp's headers were
  imported and later split into the domain headers under `include/`.
- **Names and organization**: hmbtn-decomp's symbol names and source-tree
  organization served as reference metadata when this source was restructured
  into readable modules. Function bodies were not replaced with reference
  implementations during that restructuring.

All remaining functions were decompiled and matched independently for this
project.

## PSY-Q interface declarations

`include/psyq_gte.h`, `include/psyq_libgpu.h` and `include/psyq_libcd.h`
declare the small part of the PSY-Q 4.6 programming interface that the game
code calls: primitive and vector structure layouts, GPU primitive macros, GTE
register macros, and CD library prototypes. They are reduced to what
compatibility requires, with notes such as `LIBGPU.H:392` pointing to the
equivalent definition in the user's own SDK. SDK commentary is not reproduced.

The SDK headers, libraries and compiler are not distributed here. The build
mounts the user's own SDK read-only.

`asm/nonmatchings/crt0/start.s` and `stup1.s` are the two CRT0 bootstrap
fragments that the game's GCC 2.95 cannot express in C (no epilogue-free
fall-through, and `sp` must be set before any frame exists). They are kept as
assembly so the C translation unit `src/crt0.c` can be built.

## Toolchain

The Docker image downloads and builds third-party tools under their own
licenses: splat, spimdisasm, maspsx (with the patch in `docker/patches/`),
wibo, asm-differ, m2c, decomp-permuter, objdiff and mkpsxiso.
`include/macro.inc`, `include/labels.inc` and `include/include_asm.h` derive
from splat's templates. `include/gte_macros.inc` provides GAS encodings of the
GTE instructions in place of DMPSX.

## Text codec

The human-readable text codec in `tools/text_codec.py` follows the verified
font atlas and control-code knowledge of the BTNx translation project. It
contains only the glyph-to-code mapping, not any game text.
