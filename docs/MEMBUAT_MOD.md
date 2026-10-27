# Membuat mod untuk NatureLoader

Panduan di bawah membahas format schema 1 pada tab **Peti mod**. Untuk
paket schema 2 dengan dependensi, aset, script deklaratif, dan registry lebih
lengkap, gunakan tab **Peti mod** yang sama dan [panduan API v2](MOD_API.md). Teks UI v2
sudah memakai jalur exporter native. Schema 2 juga mendukung [patch nilai item retail](NATIVE_ITEM_MODS.md); entitas baru tetap belum terhubung.

NatureLoader saat ini punya dua jalur mod:

1. `texts`: mengganti teks game retail. Mod ini bisa memengaruhi teks runtime native USA yang diuji.
2. `content`: menyimpan data item/NPC/lokasi dan tipe lain di registry pratinjau. Data ini belum dibaca gameplay.

Keduanya memakai ZIP dan `mod.json`. Mulai dari mod teks karena itulah jalur yang dapat mengubah game saat ini.

**Batas penting:** saat ini belum bisa membuat NPC, item, alat, area, bangunan,
atau sprite baru yang muncul dan berfungsi di dalam game. Tipe tersebut hanya
bisa disimpan dan dilihat sebagai JSON di tab **Konten mod**. Satu-satunya
perubahan mod yang sudah dibuktikan di game adalah mengganti teks UI yang sudah
ada pada layar pilih Memory Card.

## Mod UI teks dalam game

Buat folder `mod-ku` dengan file `mod.json`:

```json
{
  "schema": 1,
  "id": "ui-bahasa-ku",
  "name": "UI Bahasa Indonesia",
  "version": "1.0.0",
  "game": "SLUS_011.15",
  "texts": {
    "messages.selectAMemoryCardSlot": "Pilih slot\nKartu Memori"
  }
}
```

Field yang didukung:

- `schema`: angka `1`.
- `id`: huruf kecil ASCII, digit, dan tanda hubung; 1–64 karakter, mulai dengan huruf. Pakai ID unik.
- `name`: nama yang terlihat di launcher, maksimal 120 karakter.
- `version`: label versi teks, maksimal 40 karakter.
- `game`: harus `SLUS_011.15` untuk mod format ini.
- `texts`: objek pasangan ID teks dan teks pengganti.

Cari ID teks dan kapasitas aslinya di [`resources/text_bank.layout.json`](../resources/text_bank.layout.json). Jangan menebak ID: kunci yang tidak terdaftar ditolak.

Saat ekspor ke native game, setiap pengganti harus muat dalam kapasitas retail ID tersebut, termasuk penanda akhir. Semua teks aktif ditulis ke salinan RAM saat game boot. ROM dan executable sumber tidak ditimpa. Build memeriksa hash executable serta byte asli sebelum menerapkan pengganti.

Glyph yang bisa dikirim ke game dibatasi tabel glyph retail: huruf A–Z/a–z, angka, sejumlah tanda baca (`? ! : ; + - ( ) . _ ~ & , ' " %`), spasi, dan baris baru. Emoji, aksen, huruf di luar tabel, serta kontrol `{RAW:xxxx}` belum bisa diekspor. Kalau glyph atau kapasitas tidak cocok, launcher menolak boot mod dan menampilkan error; teks tidak dipotong diam-diam.

Contoh mod berisi perubahan prompt Memory Card ada di [`examples/native-ui/mod.json`](../examples/native-ui/mod.json). Contoh ini sudah diuji mengubah layar game, lalu mengembalikan teks asli sesudah mod dinonaktifkan.

## Paket ZIP

Letakkan `mod.json` langsung di root ZIP. Tambahkan `README.md` dan `LICENSE` bila perlu. Launcher menolak subfolder, file tambahan lain, symlink, hardlink, nama duplikat, dan field manifest yang tidak dikenal. Batas paket: 1 MiB per file dan 2 MiB total data setelah dekompresi.

Di Linux atau macOS, dari root proyek:

```sh
cmake -E chdir examples/native-ui cmake -E tar cf ../../build/native-ui.zip --format=zip mod.json
```

Pastikan isi ZIP menampilkan `mod.json` di tingkat teratas, bukan `native-ui/mod.json`.

## Pasang dan coba

1. Siapkan checkout runtime sesuai bagian [Menjalankan runtime native lokal](../README.md#menjalankan-runtime-native-lokal).
2. Buka **Peti mod**, pilih **Pasang mod ZIP**, lalu pilih paket.
3. Pilih mod dan klik **Aktif / nonaktif** sampai statusnya `ON`.
4. Pada **Koleksi ROM**, pilih disc USA yang didukung dan klik **Jalankan native (uji)**.
5. Untuk contoh `native-ui`, pilih **Continue** dan lanjut ke layar pemilihan Memory Card. Prompt akan berubah.

Mod teks yang dimuat ke game membutuhkan runtime lokal tersebut. Kalau hanya ingin melihat atau mengedit teks melalui penampil launcher, impor data USA pada tab **Peti mod**; runtime native tidak diperlukan.
6. Kembali ke launcher, nonaktifkan mod, lalu jalankan ulang. Prompt asli kembali.

Mod baru selalu nonaktif saat dipasang. Mod aktif diproses berurutan; mod paling bawah menang saat beberapa mod mengganti kunci teks sama. Konflik ditampilkan di tab mod.

Contoh `hello-text` sengaja panjang untuk menguji penampil teks launcher. Teks itu melebihi kapasitas retail sehingga **tidak dapat dipakai saat boot native**. Nonaktifkan sebelum menjalankan game.

## Mod konten pratinjau

Manifest boleh memakai `content` sebagai ganti atau bersama `texts`:

```json
{
  "schema": 1,
  "id": "kebun-ku",
  "name": "Konten Kebunku",
  "version": "1.0.0",
  "game": "SLUS_011.15",
  "content": {
    "kebun-ku:lobak-biru": {
      "type": "item",
      "name": "Lobak Biru",
      "data": {"sellPrice": 120}
    }
  }
}
```

ID record harus berbentuk `<id-mod>:<id-lokal>`. Tipe terdaftar: `item`, `tool`, `npc`, `location`, `dialog`, `schedule`, `story`, `building`, `sprite`, `ui_theme`, dan `rules`. Maksimal 1024 record per mod dan 64 KiB per record.

Pasang dan aktifkan lewat **Peti mod**. Tab **Konten mod** menampilkan record serta JSON yang disimpan. Saat ini `sellPrice` hanya data; ia tidak menambahkan item ke game. `ui_theme` juga belum mengubah layar. Native launch menolak mod konten aktif agar hasilnya tidak menyesatkan.

## Kekurangan dan batas saat ini

- Gameplay farm belum terbukti berjalan. Bukti native saat ini mencakup boot, title, input menu, Continue, dan layar Memory Card.
- Native launch hanya mengizinkan satu disc USA dengan hash yang sudah diaudit. ROM mod IDN dan varian executable lain tidak bisa diluncurkan lewat tombol itu.
- Mod teks native hanya mengganti teks yang sudah ada, dengan glyph dan kapasitas PS1 retail. Ia belum menambah font atau memperpanjang string.
- Mengganti sprite, menggambar ulang layout UI, menambah map/NPC/item/bangunan/alat, Lua, serta migrasi save belum bekerja di gameplay.
- Registry konten menerima data JSON, tetapi belum memvalidasi aturan tiap tipe atau membuat record baru dalam engine.
- Runtime native adalah checkout riset lokal pilihan CMake, belum dikemas bersama launcher. Tombol native tidak tersedia jika checkout/binary lokal tidak ada.
- Build launcher dan native game telah diuji di Linux. Build serta runtime Windows belum diverifikasi; Android belum dikerjakan.
- Runtime riset memakai lisensi PolyForm Noncommercial. Distribusi komersial membutuhkan izin/hak yang sesuai dan tinjauan lisensi sebelum runtime tersebut dapat dikemas.

Lihat [`MOD_CONTENT.md`](MOD_CONTENT.md) untuk detail schema registry, dan [`NATIVE_PROGRESS.md`](NATIVE_PROGRESS.md) untuk bukti serta catatan port native.
