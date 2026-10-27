# Referensi pembuatan mod (schema 2)

Schema 2 masih eksperimental. Gunakan folder lewat CLI `naturemod` atau ZIP lewat tab
**Modcore v2** di launcher. `mod.json` harus berada di akar. Contoh authoring: `examples/v2/`;
contoh teks yang memakai jalur native: `examples/native-ui-v2/`.

```sh
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF
cmake --build build-modcore --parallel
./build-modcore/naturemod check examples/v2
./build-modcore/naturemod list examples/v2
ctest --test-dir build-modcore --output-on-failure
```

Katalog ID authoring dibundel di CLI; `--catalog <file>` menggantinya untuk riset.
Exit code: `0` semua mod valid, `1` ada mod ditolak, `2` penggunaan/input alat salah.
Kelulusan pemeriksaan tidak berarti konten sudah tampil di game. Tab lama memakai schema 1;
tab Modcore v2 memiliki profil sendiri dan mengekspor mod teks ke jalur native.
Adapter schema 1 di modcore belum menggantikan loader teks lama.

## Instalasi dan profil launcher

ZIP schema 2 dapat memuat subfolder konten/script/aset. Batasnya 4096 anggota, 32 MiB per file,
64 MiB total hasil ekstraksi, dan 1 MiB untuk `mod.json`. Symlink, hardlink, path traversal,
serta nama yang bertabrakan setelah case folding ditolak.

Paket dipasang nonaktif di `mods-v2/<id>/`. Aktivasi dan pengurutan memvalidasi seluruh profil;
jika gagal, profil lama tetap dipakai. Dependensi tidak diunduh otomatis. Nonaktifkan mod
yang bergantung terlebih dahulu sebelum melepas dependensinya. File yang dilepas dipindahkan
ke `removed-mods-v2/`. `profile-v2.json` menyimpan urutan pilihan dan `authoringSave` untuk
ID authoring yang persisten; ini bukan save gameplay BTN.

```sh
./build/natureloader --data local-data --v2-install mod-ku.zip
./build/natureloader --data local-data --v2-enable mod-ku
./build/natureloader --data local-data --v2-status
./build/natureloader --data local-data --v2-disable mod-ku
./build/natureloader --data local-data --v2-remove mod-ku
```

Boot native saat ini hanya menerima teks v2 yang cocok dengan key/glyph/kapasitas retail.
Konten, script, hook, plugin, dan override aset aktif ditolak karena belum terhubung ke BTN.
Key teks yang dipakai profil v1 dan v2 sekaligus juga ditolak. Native plugin tidak mendapat
persetujuan otomatis dari launcher. Tidak ada sandbox untuk kode native.

## mod.json

| Field | Wajib | Keterangan |
| --- | --- | --- |
| `schema` | ya | `2` (schema `1` lama tetap dibaca lewat adapter) |
| `id`, `name`, `version` | ya | id: `[a-z][a-z0-9_-]{0,63}`, `base` dicadangkan; versi `MAJOR.MINOR.PATCH` |
| `game` | ya | `SLUS_011.15` |
| `api` | ya | rentang API modding, mis. `">=1.0 <2"`, `"^1.0"` |
| `depends` / `optional` | tidak | `{ "id-mod": "rentang" }` |
| `load_after` / `load_before` | tidak | daftar id mod |
| `conflicts` | tidak | `{ "id-mod": "rentang" }`; kedua mod ditolak |
| `content` | tidak | array path JSON konten, atau objek konten inline |
| `scripts` | tidak | array path script deklaratif |
| `hooks` | tidak | hook global: `{"on":"day.start","handler":"nama","subject":"","priority":0}` |
| `permissions` | tidak | `inventory`, `player`, `world`, `ui`, `save` |
| `assets.dir` | tidak | folder aset (default `assets`) |
| `plugins` | tidak | `{file, sha256, exe_hashes[], api}` - lihat plugin native |
| `save_version` | tidak | naikkan saat format data save mod berubah |

## Konten

File konten: `{ "<jenis>": { "<id-lokal>": { ...data } } }`. ID penuh = `<id-mod>:<id-lokal>`.
`$patch` mengubah field record milik `base:` atau mod yang dideklarasikan.

Jenis: `item`, `npc`, `area`, `shop`, `crop`, `building`, `event`, `recipe`, `dialog`, `schedule`,
`sprite`, `asset_override`, `language`, `rules`, `ui_theme`.

Batas angka mengikuti tipe data game (`ItemInfo` di decomp): `buy_price`/`sell_price` s16,
`stamina`/`fatigue` s8. Nilai di luar batas ditolak saat muat, bukan dipotong diam-diam.

Item: `name` (string atau `{en,id}`), `category` (`crop|tool|seed|consumable|object`), harga,
`stamina`, `fatigue`, `sprite` (id aset), `inherit` (id item untuk salinan baris dasar), `flags`
(`pickup|tool|seed`), `hooks` (`use`, `on_buy`, `on_sell`).

Area: `map` (aset), `entrances[{id,x,y}]`, `exits[{rect:[x,y,w,h], to|to_section, entrance}]`, `npcs`,
`hooks` (`on_enter`, `on_exit`). `to` merujuk area mod; `to_section` menuju area retail lewat nomor section.
Perpindahan area dirujuk lewat ID stabil, bukan nomor tabel.

Aset: file di `assets/` menjadi id `<mod>:<path>`. Override base:
`{"target":"base:tex/11/3","file":"hd-pack:tex/player-hd.png","scale":4,"pivot":[64,180]}`.

## Script deklaratif

`{"handlers": {"nama": [aksi, ...]}}`. Aksi: `give_item`, `remove_item`, `show_text`, `add_stamina`,
`add_money`, `set_flag`, `set_var`, `if`(`cond`,`then`,`else`), `repeat`(`times`,`do_list`), `warp`,
`cancel`, `set_result`, `call`, `emit`, `log`.
Ekspresi: literal, `{"var":..}`, `{"ctx":"args.path"}`, `{"flag":..}` (flag milik mod sendiri;
belum diset = 0 pada aritmetika), `{"has_item":..}`, `{"money":1}`, `{"day":1}`, `{"random":n}`,
`{"concat":[..]}`, `{"not":..}`, `{"op":"+|-|*|/|%|==|!=|<|<=|>|>=|and|or","a":..,"b":..}`.
Nama item tanpa `:` dianggap milik mod sendiri.

## Hook

`game.start`, `day.start`, `day.end`, `item.use`, `item.buy`, `item.sell`, `area.enter`, `area.exit`,
`npc.talk`, `npc.gift`, `crop.harvest`, `event.run`, `save.before`, `save.after`, `save.migrate`.
Hook yang bisa dibatalkan (`cancel`): `item.use/buy/sell`, `area.enter`, `npc.talk`, `event.run`.

## Plugin native

Hanya jika script tidak cukup. Implementasikan `modcore/nature_plugin.h`. Manifest harus memuat SHA-256
biner dan SHA-256 executable game yang cocok; pengguna harus menyetujui biner+versi tersebut. Contoh:
`tests/modcore/plugin_sample.c`.
