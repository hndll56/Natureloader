# Menghubungkan core hasil port ke modcore

Titik di bawah ini dibaca dari `hmbtn-decomp` yang diunggah (bukan ditebak). Semuanya belum
dipasang pada gameplay: ini daftar kerja untuk core PC. Teks serta empat field item retail sudah diteruskan lewat exporter native.
Pengembangan tabel untuk entitas baru dan hook gameplay di bawah masih belum disambungkan.

## Tabel dan akses data

Empat field pada item retail kini sudah memakai adapter exporter RAM; lihat
[mod item native](NATIVE_ITEM_MODS.md). Alokasi tabel untuk item baru dan hook
gameplay pada daftar di bawah masih merupakan pekerjaan port berikutnya.

| Kebutuhan | Lokasi decomp | Pekerjaan |
| --- | --- | --- |
| Tabel item | `main/game/farm_tables.c` `item_info[ITEM_COUNT=380]`; `game_state.itemInfo` adalah **pointer** (`farm_state.h`) | Alokasikan tabel sebesar `nm_kind_count("item")`, salin baris retail, isi baris mod dari `nm_item_row` (jika `inherit_handle >= 0`, salin baris itu dulu). Pointer sudah ada, jadi akses `game_state.itemInfo[id]` tidak berubah. |
| Lebar ID item | `typedef u16 ItemId`; `FarmStoredItem.id` (u16/s16) | Handle mod mulai `0x200` sampai `0x7FFF`; muat di s16/u16. Periksa field lain yang memampatkan ID (event memakai 10 bit pada `event_script.h`). |
| Harga | `item_buy_price`, `item_sell_price` (`picture_cells.c`) | Sudah membaca tabel; tidak perlu hook. Hook `item.buy/sell` opsional. |
| Crop | `ItemInfo.crop` bertipe `s8`, `CROP_COUNT 22` | Handle crop mod 22..126 sampai field diperlebar. |
| Teks | `main/text/text_bank.c` | Mod bahasa: `language` + penggantian `texts` lama; kapasitas glyph retail tetap batasannya. |

## Hook

| Hook | Panggil `nm_fire` dari | Catatan |
| --- | --- | --- |
| `item.use` | `item_run_use(FarmStoredItem*)` di `picture_cells.c` | Subjek = ID string dari handle (`nm_id`). Jika hasilnya 1 (cancel), lewati `callback` retail. |
| `area.enter` / `area.exit` | `scene_change_area(u16 area, ...)` di `world/scene.c` | Rujuk area dengan ID stabil; mod area perlu loader section sendiri (lihat bawah). |
| `day.start` / `day.end` | `player_new_day_reset` (`people/player.c`) / `player_go_to_sleep`, `farm_end_of_day` (`animals/animals.c`) | Pastikan sekali per hari. |
| `npc.talk` | `event_cmd_talk_end` dan jalur awal percakapan di `event/event_commands.c` | `cancel` melewati dialog retail. |
| `save.before/after` | `overlay/15_saveload/saveload.c` | Panggil `nm_save`, tulis sidecar di samping slot. |

## Bahaya port 64-bit yang terlihat

- `NmItemRow` saat ini mengisi field yang tidak disediakan dengan nol. Saat memakai `inherit`,
  adapter core perlu membaca `nm_record_json` untuk mengetahui field yang benar-benar dioverride.
  Jangan menimpa seluruh baris warisan dengan field default dari `NmItemRow`.

- `item_run_use` memperlakukan `&record->use` sebagai `FarmDispatchRecord*` (callback + tiga argumen
  dikemas di tabel). Pada host 64-bit pointer tidak 4 byte, jadi tata letak ini harus diganti struct
  eksplisit sebelum tabel dibangun ulang.
- Memori tetap `0x801BE1B0` dan pointer sebagai integer (catatan `docs/PORTING.md`).

## Yang belum terjawab (perlu analisis lanjut sebelum area/NPC mod)

- Area retail dimuat sebagai *section* dari arsip (`farm_load_section(area, cached)`), bukan tabel bernama.
  Area mod butuh jalur muat yang membaca `map`/`collision` dari VFS, plus peta tabrakan dan model.
- Daftar NPC/villager, event (id 10 bit), jadwal: rentang handle di `base_catalog.json` masih perkiraan.
- Jalur tekstur: `DecodeContainer0D` (`graphics/archive_3.c`). Override aset sebaiknya dicek sebelum
  decoder, memakai `nm_asset_override("base:tex/<member>/<index>")`; penamaan `base:tex/...` perlu
  dikunci terhadap indeks arsip `A_FILE`.
