# Native item model (modcore slice)

Status: **modcore model only. The native BTN runtime is not involved and nothing here is proven in it.**

This milestone implements the item half of the hybrid-runtime plan as a headless, deterministic model inside
`modcore/`. It extends the existing `Session`, `Registry`, `IdTable`, hook bus and `SaveExtension`. There is no
second registry and no second mod system.

## What exists (proven by `tests/modcore/native_item_tests.cpp`)

| Piece | File | Notes |
| --- | --- | --- |
| Vanilla id adapter | `modcore/vanilla_ids.*` | Normalises `vanilla:` aliases to the repository's canonical `base:<local>` ids. |
| `ItemHandle` | `modcore/native_items.h` | Opaque, not constructible from numbers, no public value, never a PS1 address. |
| `NativeItems` | `modcore/native_items.*` | Resolve id/alias, read fields, load sprite bytes. Read-only view over a loaded `Session`. |
| `NativeInventory` | `modcore/native_items.*` | Stacks keyed by canonical id, capacity, reserved (orphan) slots, acquisition order. |
| `NativeItemRuntime` | `modcore/native_items.*` | acquire, remove, buy, select, use, set state, host bridge for scripts, load/save state. |
| `NativeSaveStore` | `modcore/native_save.*` | `natureloader.json`, `mods/<mod>.json`, atomic writes, `.bak`, quarantine of damaged files. |
| Example mod | `examples/native-items/expanded/` | Registers `expanded:golden_turnip` with an original placeholder sprite. |

## Id convention (decision)

The repository convention is unchanged: retail content is `base:<local>` (exactly one `:`, local id
`[a-z][a-z0-9_-]{0,63}`), for example `base:turnip` = item 65. That string is canonical in the registry,
save files, manifests and the C ABI.

The architecture notes also use `vanilla:item.turnip` and `vanilla:npc:elli`. Those spellings are accepted
**only at native-layer entry points as aliases** and rewritten before lookup. They are never stored.

| Alias | Registry kind | Canonical |
| --- | --- | --- |
| `vanilla:item.turnip` / `vanilla:item:turnip` | `item` | `base:turnip` |
| `vanilla:npc:elli` | `npc` | `base:elli` |
| `vanilla:map:farm` | `area` (`map` is an alias of `area`) | `base:farm` |

Compatibility tests prove that all spellings reach the same record (`base:turnip`, handle 65), that the C ABI
still rejects `vanilla:item.turnip` (`nm_handle` = -1), and that aliases never reach mod content
(`vanilla:item.golden_turnip` resolves to `base:golden_turnip`, which does not exist).

Limits: the base catalog names only items today, so `vanilla:npc:elli` and `vanilla:map:farm` normalise but find
no record. A mod literally named `vanilla` would make the alias ambiguous; the native layer then refuses all
aliases and that mod's own ids (`vanilla:x`) are unreachable through it. Reserving `vanilla` in manifest
validation is future work and would be a separate, global change.

## Behaviour contract

- **Registration**: items come from the manifest/content through the existing phases. `NativeItems` never registers.
- **Handles**: value is the registry handle from the persisted `IdTable`; equal across reloads, saves, load-order
  changes and disable/re-enable. A handle whose mod is not loaded fails `info()` instead of dangling.
- **Inventory**: model constants `kMaxStack = 99`, `kDefaultSlots = 24` (not retail values). Adds are all-or-nothing.
  Vanilla items are refused with `VanillaNotBridged`: the retail inventory lives in PS1 memory and no bridge exists.
- **Use** (`item.use`): the hook runs first. A handler that calls `cancel` takes over completely: no default effect,
  nothing consumed. Otherwise the default applies the item's `stamina`/`fatigue` fields to the `PlayerPort` and
  consumes one unit. Handler faults are isolated, counted in `hookFaults`, and never block the default.
  An item is usable if it has `hooks.use` or a non-zero stamina/fatigue. Hooks bound through manifest `hooks` with a
  subject are not detected by that check.
  Note: `examples/v2/kebunku` sets `stamina` and also adds stamina in its hook; under this runtime that double-applies.
- **Buy** (`item.buy`): validate, check affordability, fire the cancelable hook, validate again, charge, add.
  Refusals (`InventoryFull`, `StackFull`, `NotEnoughMoney`, `NotForSale`, cancel) never charge.
- **Select**: by slot, tracked by id so removals cannot leave a stale index. Selection is transient and not saved.
- **Host bridge**: `give_item`, `remove_item`, `add_stamina`, `add_money`, `show_text`, `log`, `random` (deterministic)
  act on the model. `warp` is not implemented.
- `item.inherit` of retail rows is not resolved by the model: absent fields read as 0.

## Save layout

```text
<save dir>/
  vanilla.mcr          never read or written by this code
  natureloader.json    SaveExtension (mods+versions, IdTable, flags, mod_data)
  mods/expanded.json   {"format":1,"mod":"expanded","version","save_version","items":{id:{count,seq,state}}}
```

- Items are keyed by canonical id, not by number. `seq` keeps acquisition order across reloads and across mod files.
- Writes are atomic (temp + rename); the previous file becomes `<name>.bak`. A file whose content did not change is
  not touched at all. No fsync: power loss can lose the newest save, not corrupt the previous one.
- Windows replaces an existing target with `MoveFileExW`; Linux replacement is tested, Windows remains unverified.
- A save-directory scan error fails before clearing live inventory or writing core state. Quarantine validates the mod ID.
- Orphans: the file of a mod that is not loaded is never rewritten (tests assert byte-identical content). Its entries
  keep their inventory slots (`reservedSlots`) so re-enabling cannot overflow. The IdTable keeps the handle reserved.
- Entries of a loaded mod that cannot become live (unknown id, invalid data, foreign prefix) are preserved verbatim,
  as are unknown top-level fields.
- A damaged or unrecognised file is never overwritten silently: it is left alone if there is nothing to store, else
  moved to `<name>.json.damaged[-n]` before a fresh file is written.
- A file with a `save_version` newer than the loaded mod is read-only; saving a stack of that mod fails loudly.
- Older `save_version` loads as-is with a note; **no migration is implemented** (`save.migrate` is not called).
- `saveState` writes `natureloader.json` first, then each mod file. It is not transactional across files.
- Capacity: loading never drops stacks because of capacity; new stacks need free slots.

## Not proven / not implemented

- Nothing runs in the native BTN runtime: no on-screen inventory, no sprite rendering, no farm, no retail save bridge.
- `ModelPlayer` is a test double. Real stamina, fatigue, money and day come from vanilla state through a bridge that
  does not exist; `vanilla.mcr` is never touched.
- No shop integration (`shop` records are not consulted), no selling, no crops, no NPC/map slices.
- Manifest modes, a native event bus beyond the existing hooks, input actions and native rendering are untouched.
- The C ABI has no entry points for this model.
- Local follow-up passes the Qt/ROM suite and native retail-patch audit (see `docs/NATIVE_PROGRESS.md`).
  Those checks do not exercise this inventory inside BTN.

## Running the tests

Local follow-up passes 17 item-model test groups, the full 18-test suite,
11 ASan/UBSan tests (leak detection disabled), and eight Python audit-tool tests.
Native New Game coverage is tested separately; no test yet proves this item in BTN.

```sh
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-modcore --parallel
ctest --test-dir build-modcore --output-on-failure
```

Sanitizers: add `-DCMAKE_C_FLAGS`, `-DCMAKE_CXX_FLAGS`, `-DCMAKE_EXE_LINKER_FLAGS` and `-DCMAKE_SHARED_LINKER_FLAGS`
set to `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer`.
