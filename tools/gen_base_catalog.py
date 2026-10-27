#!/usr/bin/env python3
"""Generate modcore/data/base_catalog.json from the decomp's include/items.h.

The catalog gives every retail item a stable string id (base:<kebab-name>) and its
retail numeric id. It contains names and numbers only - no game data.
Usage: gen_base_catalog.py <path/to/hmbtn-decomp/src/include/items.h> <out.json>
"""
import json, re, sys

def main(src, out):
    text = open(src, encoding="utf-8").read()
    names = {}
    for m in re.finditer(r'\bITEM_([A-Z0-9_]+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)', text):
        name, val = m.group(1), int(m.group(2), 0)
        if name == "COUNT":
            continue
        names.setdefault(val, name.lower().replace("_", "-"))
    cat = {
        "format": "nature-base-catalog-v1",
        "source": "hmbtn-decomp src/include/items.h",
        "kinds": {
            # retail table size and the first handle handed out to mods.
            # ItemId is u16 in the decomp; ids up to 0x186 exist, 0x200 leaves headroom.
            "item": {"retail_count": 380, "mod_first": 0x200, "mod_last": 0x7FFF,
                      "names": {str(k): v for k, v in sorted(names.items())}},
            # provisional ranges: areas are u16 (scene_change_area); NPC/event ranges
            # still have to be confirmed against the villager/event tables.
            "area": {"retail_count": 0x400, "mod_first": 0x400, "mod_last": 0xFFFD, "names": {}},
            "npc": {"retail_count": 0x100, "mod_first": 0x100, "mod_last": 0x7FFF, "names": {}},
            "crop": {"retail_count": 22, "mod_first": 22, "mod_last": 126, "names": {}},
            "building": {"retail_count": 0x40, "mod_first": 0x40, "mod_last": 0x7FFF, "names": {}},
            "event": {"retail_count": 0x400, "mod_first": 0x400, "mod_last": 0x7FFF, "names": {}},
            "shop": {"retail_count": 0x20, "mod_first": 0x20, "mod_last": 0x7FFF, "names": {}},
            "recipe": {"retail_count": 0x80, "mod_first": 0x80, "mod_last": 0x7FFF, "names": {}},
        },
    }
    # crop: CROP_COUNT is 22 (farm_tables.h) and ItemInfo.crop is s8, so mods get 22..126
    # until that field is widened (see docs/DECOMP_INTEGRATION.md).
    json.dump(cat, open(out, "w"), indent=1, sort_keys=True)
    print(f"{len(names)} item ids -> {out}")

if __name__ == "__main__":
    main(*sys.argv[1:3])
