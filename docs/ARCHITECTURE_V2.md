# Arsitektur modding v2: core, registry, API, hook

Dokumen ini menjelaskan apa yang **tersedia sebagai runtime eksperimental** di `modcore/` dan apa yang masih harus
dikerjakan di sisi core hasil port. Tidak ada klaim bahwa game BTN sudah bisa dimainkan dengan mod;
yang terbukti adalah runtime mod itu sendiri, diuji lewat core tiruan yang hanya memakai C ABI.

## Gambaran

```text
launcher / CLI (naturemod)            game core (hasil port decomp)
        |                                      |
        |  preflight: versi, dependensi,       |  hanya melihat modcore/capi.h (C ABI)
        |  konflik, urutan muat                |
        v                                      v
 +-------------------------- Session (modcore/session.*) --------------------------+
 | 1 manifest+resolve  2 daftar ID  3 validasi  4 handle  5 aset  6 script+hook  7 plugin |
 +-----------+------------------+---------------+-----------------+----------------+
             |                  |               |                 |
        Registry (katalog)   Vfs (aset)    HookBus + ActionEngine   PluginHost
        id string <-> handle  override base  titik panggil core      gerbang 4 syarat
             |
        SaveExtension: id mod+versi, IdTable (handle stabil), flag, data per mod
```

Registry adalah katalog, API (`capi.h`) adalah sambungan ke gameplay, hook adalah titik saat core
meminta perilaku mod. Menyimpan definisi item saja tidak membuat item muncul: core harus
(a) membangun tabelnya dari registry, (b) memanggil `nm_fire` di titik hook.

## Urutan fase pemuatan

Urutan penting karena dialog/event/toko merujuk konten mod lain.

| Fase | Isi | Gagal berarti |
| --- | --- | --- |
| 1 resolve | game ID, rentang API, dependensi, konflik, siklus, urutan muat | mod ditolak + semua yang bergantung padanya |
| 2 daftar ID | seluruh konten semua mod masuk registry, **baru kemudian** `$patch` | ID ganda, jenis tidak dikenal, patch ke target hilang |
| 3 validasi | schema per jenis (lebar tipe data game), referensi silang, aturan dependensi | mod ditolak |
| 4 handle | ID string -> nomor untuk tabel retail, memakai `IdTable` dari save | rentang habis |
| 5 aset | indeks aset pack, validasi PNG, override aset base | aset hilang/rusak |
| 6 script+hook | validasi script, izin, handler ada, pasang hook | handler hilang, aksi tidak dikenal |
| 7 plugin | hash biner + hash exe + rentang API + persetujuan pengguna | plugin ditolak |

Jika sebuah mod gagal di fase mana pun, **seluruh build diulang tanpa mod itu** (dan dependennya).
Tidak ada keadaan setengah termuat. Pesan error dari percobaan sebelumnya tetap muncul di laporan.

## Tambahan terhadap rancangan awal

1. **Izin per mod** (`permissions`: `inventory`, `player`, `world`, `ui`, `save`). Script hanya bisa
   memanggil layanan host yang izinnya dideklarasikan; diperiksa statis saat muat dan lagi saat jalan.
2. **Anggaran langkah yang tidak bisa ditangkap.** Script deklaratif tidak punya `pcall`; habisnya
   anggaran (20.000 langkah), rekursi `call` > 8, atau `repeat` > 1000 menghentikan handler seketika.
   Ini menutup celah yang ditemukan pada patch Lua sebelumnya (loop lewat `pcall`).
3. **Isolasi dan pemutus sirkuit.** Handler yang gagal dicatat; setelah 5 kegagalan, hook milik mod
   itu dibisukan selama sesi. Game dan mod lain tetap jalan.
4. **Handle stabil lewat `IdTable` di save.** Nomor yang pernah diberikan tidak berubah walau urutan
   muat berubah, dan nomor milik mod yang hilang dicadangkan (tidak dipakai ulang mod lain).
5. **Aturan dependensi referensi.** Mod hanya boleh merujuk `base:`, dirinya, atau mod yang
   dideklarasikan di `depends`/`optional`/`load_after`. Referensi diam-diam ke mod lain ditolak.
6. **`inherit`.** Item mod dapat menyalin baris retail (`base:turnip-seeds`) lalu menimpa sebagian
   field; core menerima `inherit_handle` di `NmItemRow`.
7. **Patch tingkat field untuk konten base** (`$patch`), dengan peringatan bila dua mod menimpa
   field yang sama (yang terakhir menang).
8. **Override aset di batas pemuatan aset**, dengan `scale` dan `pivot` agar sprite HD punya metadata.
   Hanya target `base:tex/`, `base:sound/`, `base:font/` yang boleh di-override.
9. **Bahasa sebagai konten** (`language`, nama bertingkat `{en,id}`), dengan fallback locale.
10. **Plugin native dengan gerbang empat syarat**: SHA-256 biner cocok manifest, SHA-256 executable game
    terdaftar, rentang API cocok, dan pengguna menyetujui tepat biner+versi itu. Salah satu gagal
    -> tidak dimuat (tidak ada patch ke build yang salah).
11. **Ekstensi save terpisah.** Runtime mod dapat membuat JSON ekstensi save. Launcher menyimpan
    state ID authoring di profil v2; core BTN masih perlu mengikat sidecar ke slot save retail.
12. **Save korup tidak menjatuhkan host.** Semua tipe divalidasi sebelum dipakai; save ditolak dengan
    pesan, sesi tetap berjalan dengan tabel baru.

## Perilaku save dan mod hilang

`nm_load(save)` membandingkan mod di save dengan mod yang termuat dan menulis laporan, misalnya:
`Mod hilang: kebunku (1.0.0). Konten tidak tersedia: item kebunku:lobak-biru, ...`.
Handle lama tetap ada di `IdTable`; `nm_orphan(kind, handle)` memberi tahu core ID stabil yang pernah
dipakai agar UI bisa menampilkan placeholder dan **tidak membuang slot**. Flag dan `mod_data` mod yang
hilang tetap tersimpan; saat mod dipasang lagi, nomor dan flag pulih (diuji di `fake_core`).

## Yang diuji (dan bagaimana)

- `tests/modcore/modcore_tests.cpp` (13 kelompok): JSON ketat, rentang versi, manifest, resolver
  (kaskade, konflik simetris, siklus, urutan), batas tipe data, referensi, handle stabil, budget script,
  izin, isolasi hook, save korup, PNG/override aset, isolasi mod gagal, patch, gerbang plugin
  (membuka `.so` sungguhan), vektor SHA-256.
- `tests/modcore/fake_core.cpp`: core tiruan memakai **hanya C ABI**: tabel item dari registry,
  toko dari registry, sprite lewat API aset, override HD, hook area/item/NPC/hari, save, mod dilepas,
  mod dipasang lagi, save rusak.
- Pengujian integrasi NatureLoader juga memeriksa path paket, error C ABI, batas script,
  umur plugin, dan kegagalan CLI. Tes `modcore_library` memeriksa alur ZIP/profil launcher.
- `naturemod check examples/v2` memuat empat mod contoh.

## Model item native (irisan modcore)

Dokumen lengkap: `NATIVE_ITEM_RUNTIME.md`. Ringkasnya: `native_items.*` menambah `ItemHandle` opak, inventory
tanpa renderer, `use`/`buy` lewat hook `item.use`/`item.buy`, dan `NativeSaveStore` (`natureloader.json` +
`mods/<mod>.json`) di atas `Session`, `Registry`, `IdTable`, dan `SaveExtension` yang sudah ada. Tidak ada
registry kedua. ID retail kanonik tetap `base:<nama>`; ejaan `vanilla:item.turnip` hanyalah alias di lapisan native
(`vanilla_ids.h`) yang diubah ke ID kanonik sebelum lookup dan tidak pernah disimpan. Data mod yang tidak aktif
tidak pernah ditulis ulang dan tetap menempati slot (`reservedSlots`). Semua ini hanya terbukti di `modcore`
terhadap `ModelPlayer` tiruan, **bukan** di runtime native BTN.

## Batas yang jujur


- Core game belum menjalankan modloader ini. `fake_core` membuktikan kontrak, bukan gameplay BTN.
- Rentang handle area/NPC/event/building/shop/recipe masih **perkiraan** (lihat `base_catalog.json`);
  hanya item (`ItemId` u16, maks 0x186) dan crop (`CROP_COUNT` 22, `ItemInfo.crop` s8) diambil dari decomp.
- Lua belum disertakan (tidak ada sumbernya di lingkungan ini). `ActionEngine` deklaratif adalah backend
  default; backend Lua dapat dipasang lewat `HookBus::bind` tanpa mengubah format mod.
- `SaveExtension::baseSaveHash` belum diisi; pengikatan sidecar ke save retail masih tugas core.
- Pembaca ZIP untuk `PackSource` belum ditulis di `modcore` (launcher sudah memakai libarchive);
  saat ini paket berupa folder (`DirPackSource`) atau memori.
- Modcore adalah backend internal **Peti mod**, bukan modloader terpisah. Schema 1/2
  berbagi pemasangan, profil, resolver, registry yang terlihat, dan exporter native.
  Native mendukung teks dan empat field item retail; entitas serta hook baru masih ditolak.
  `-DNATURE_BUILD_LAUNCHER=OFF` membangun alat authoring tanpa Qt/libarchive.
- Pengujian `fake_core` tidak membaca disc atau menjalankan gameplay BTN.
- ABI dan format schema 2 masih eksperimental. Plugin native menjalankan kode host penuh;
  persetujuan hash bukan sandbox. CLI tidak memberikan persetujuan plugin otomatis.
- Validasi PNG memeriksa header/dimensi, bukan decode penuh atau kesiapan sprite untuk renderer BTN.
- Model item native (`native_items.*`) belum terhubung ke runtime BTN: tidak ada inventory di layar, farm,
  jembatan ke save retail, integrasi toko, migrasi `save_version`, maupun entry C ABI. Item retail ditolak inventory
  (`VanillaNotBridged`). Mod bernama `vanilla` membuat alias ambigu sehingga alias dinonaktifkan.
