# NatureLoader

Panduan membuat paket mod: [`docs/MEMBUAT_MOD.md`](docs/MEMBUAT_MOD.md).

Fondasi launcher desktop dan port native Harvest Moon: Back to Nature.
Target utama: Windows dan Linux. Android ditunda sampai runtime desktop stabil.

**Versi 0.2 masih eksperimental.** Build riset Linux lokal kini menjalankan boot,
title, dan menu Memory Card game asli melalui kode native. Gameplay farm lengkap,
save/reload, Windows runtime, dan ROM mod IDN belum diverifikasi. Runtime native
riset belum dibundel dalam distribusi launcher.

## Yang sudah bekerja

- Tampilan pedesaan dengan ilustrasi kebun asli, papan kayu, dan panel krem.
- Tab **Koleksi ROM** untuk menyimpan ROM asli dan ROM mod sebagai versi terpisah.
  Format sektor diperiksa dari isi file, bukan ekstensi. ISO mod Indonesia
  contoh pengguna terbaca sebagai Mode 2/2352 dengan 33 anggota arsip.
- Identitas SHA-256 seluruh disc, pemilihan versi yang persisten, pemeriksaan
  salinan lokal dan CUE, serta direktori profil terpisah untuk setiap ROM.
  Status tersimpan tidak berarti versi tersebut sudah bisa dimainkan native.

- Launcher native C++17 / Qt 6, tanpa Python sebagai dependensi runtime.
- Impor ZIP berisi satu BIN/ISO, BIN Mode 2/2352, ISO sektor 2048 byte,
  atau CUE satu track MODE2/2352 dengan INDEX 01 di 00:00:00.
- Verifikasi SHA-256 executable USA `SLUS_011.15` dan bank teks yang dikenal.
- Pembacaan root ISO9660, ekstraksi lokal executable, `A_FILE.HDT`, dan
  `A_FILE.BIN`; pemeriksaan batas 32 anggota arsip.
- Pembacaan 354 entri teks dari ROM milik pengguna, ditampilkan di desktop.
- Instalasi mod ZIP, aktif/nonaktif, urutan prioritas, dan laporan konflik.
  Mod aktif paling bawah menang. Pengaturan bertahan setelah launcher ditutup.
- Perbandingan teks asli tanpa mengubah mod aktif, serta pelepasan mod ke
  `removed-mods/` agar file mod masih bisa dipulihkan.
- Penggantian teks di penampil native tanpa patch ROM, rebuild, atau batas
  kapasitas string retail. Batas prototipe adalah 16384 karakter per override.

**Mod teks dapat diekspor ke RAM game native; mod konten masih registry pratinjau.**
Mod teks native dibatasi glyph dan kapasitas string retail. Mod sprite HD dan
penambahan map/NPC/item/bangunan/alat belum terhubung. Input menu native sudah
diuji; gameplay farm, audio, dan save/reload belum diverifikasi.
File data lain seperti XA/STR belum diimpor. Kontrol teks yang belum ditafsirkan
ditampilkan sebagai `{RAW:xxxx}`. Ini bukan renderer dialog game yang lengkap.

Katalog ROM menyimpan salinan lengkap disc, termasuk media di dalamnya. Jalur
impor data USA untuk penampil teks tetap mengekstrak tiga file seperti sebelumnya.
Mod teks di tab **Peti mod USA** dapat diterapkan pada runtime USA lokal.
ROM mod dengan executable berbeda belum dapat dijalankan.
Komponen port grafis awal `src/texture_decoder.*` dapat mendekode kontainer 0D
secara native, termasuk RLE baris/kolom. Tab aset menyediakan pemilihan blok, CLUT, 4/8 bpp, dan ekspor PNG pada ukuran
asli hasil decode. Integrasi rendering gameplay belum selesai.

## Menjalankan runtime native lokal

Build proyek ini dikonfigurasi memakai checkout riset lokal `reference/btn-recomp`.
Pada tab **Koleksi ROM**, pilih disc USA yang sudah diimpor, lalu klik
**Jalankan native (uji)**. Launcher memverifikasi hash penuh sebelum menjalankan
runtime. ROM IDN belum memiliki cakupan kode native yang cocok dan tombolnya
masih nonaktif. Enter adalah Start; X konfirmasi, panah untuk arah.

Untuk konfigurasi ulang development:

```sh
cmake -S . -B build -DNATURE_NATIVE_RUNTIME_DIRECTORY="$PWD/reference/btn-recomp"
cmake --build build --parallel 2
```

Opsi ini menghubungkan checkout lokal yang sudah dibangun; bukan pengunduh atau
installer runtime. Tanpa checkout tersebut, launcher tetap dapat dipakai sebagai
pengelola ROM/mod, tetapi tombol native dinonaktifkan. Menutup launcher juga
menutup proses game yang dikelolanya. Log dan save dipisahkan per ROM di
`roms/<hash>/profile-data/native-saves/`.

Contoh mod UI game: kemas `examples/native-ui/mod.json`, pasang ZIP, lalu aktifkan.
Saat game dijalankan, launcher menyusun snapshot mod terpisah dan runtime
menerapkannya ke RAM dengan pemeriksaan byte asli. Buka **Continue** di title
untuk melihat teks pemilihan Memory Card berubah menjadi `Pilih slot` dan
`NatureLoader!`. Nonaktifkan mod lalu jalankan ulang untuk kembali ke teks asli.

```sh
cmake -E chdir examples/native-ui cmake -E tar cf ../../build/native-ui.zip --format=zip mod.json
```

Semua mod aktif diperiksa sebelum boot. Mod konten yang belum didukung, glyph
non-retail, dan teks melebihi kapasitas ditolak dengan pesan; tidak diabaikan
secara diam-diam. Nonaktifkan contoh `hello-text` yang panjang sebelum menguji
runtime. Penggantian sprite HD, layout UI, Lua, dan penambahan entitas belum
terhubung. Export debug CLI tersedia melalui `--native-export <USA-executable>`.

## Build Linux

Diperlukan compiler C++17, CMake 3.21+, Qt 6 Widgets beserta plugin platform,
dan libarchive development headers. Gunakan paket distribusi untuk dependensi
tersebut, misalnya `qt6-base-dev` dan `libarchive-dev` di Debian/Ubuntu.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/natureloader
```

Data default menggunakan direktori aplikasi Qt. Untuk data proyek lokal:

```sh
./build/natureloader --data local-data
```

Build Linux sudah diuji di lingkungan pengembangan. Binary masih bergantung
pada Qt dan libarchive di mesin tujuan; paket AppImage belum dibuat.

## Build Windows

Gunakan Visual Studio 2022 dengan workload Desktop development with C++,
CMake, Qt 6 untuk MSVC x64, dan libarchive x64 dari vcpkg. Jalankan dari
Developer PowerShell. Sesuaikan lokasi Qt dan vcpkg dengan instalasi sendiri:

```powershell
vcpkg install libarchive:x64-windows
cmake -S . -B build-win -A x64 `
  -DCMAKE_PREFIX_PATH="$env:QT_ROOT" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-win --config Release --parallel
& "$env:QT_ROOT/bin/windeployqt.exe" build-win/Release/natureloader.exe
& "$env:QT_ROOT/bin/windeployqt.exe" build-win/Release/nature_tests.exe
ctest --test-dir build-win -C Release --output-on-failure
./build-win/Release/natureloader.exe
```

`QT_ROOT` menunjuk direktori kit MSVC Qt, bukan direktori semua versi Qt.
vcpkg app-local deployment harus menyediakan DLL libarchive dan dependensinya.
Paket distribusi perlu menyertakan DLL, plugin platform Qt, serta notice lisensi
dependensi. Build Windows dan installer belum diverifikasi pada Windows asli.

## Mencoba mod pertama

1. Jalankan launcher, pilih **Impor ROM**, lalu pilih disc USA milik sendiri.
2. Buat ZIP yang berisi `examples/hello-text/mod.json` tepat di root ZIP.
3. Pilih **Pasang mod ZIP**, pilih mod, lalu klik **Aktif / nonaktif**.
4. Pilih `messages.promptYesNo`. Sumber berubah dari ROM menjadi `hello-text`.
5. Nonaktifkan mod untuk melihat teks asli kembali tanpa mengimpor ulang ROM.

Contoh pengemasan dengan CMake, dari root proyek:

```sh
cmake -E chdir examples/hello-text cmake -E tar cf ../../build/hello-text.zip --format=zip mod.json
```

Contoh manifest:

```json
{
  "schema": 1,
  "id": "hello-text",
  "name": "Halo dari NatureLoader",
  "version": "1.0.0",
  "game": "SLUS_011.15",
  "texts": {
    "messages.promptYesNo": "Ya, pakai mod ini!\nTidak dulu."
  }
}
```

ID harus diawali huruf kecil dan hanya berisi huruf kecil, digit, atau tanda
hubung; panjang maksimal 64 karakter. Kunci teks berasal dari
`resources/text_bank.layout.json`. `mod.json` wajib berada di root ZIP;
`README.md` dan `LICENSE` boleh disertakan dan dipertahankan saat instalasi.
Script, DLL, dependensi antarmod, dan field manifest tak dikenal ditolak. Versi format ini masih prototipe, bukan SDK stabil.

Mod baru dipasang dalam keadaan nonaktif. ID yang sudah terpasang ditolak.
Untuk melepas mod, pilih mod lalu klik **Lepas mod terpilih**. File dipindahkan
ke `removed-mods/` dan keluar dari
profil aktif. UI pemulihan dan upgrade belum tersedia; salinan ZIP asli dapat
dipasang lagi setelah pelepasan.

## Mod konten eksperimental

Tab **Konten mod** menampilkan registry dari mod aktif, dengan pencarian ID dan
pratinjau JSON. Instalasi, aktivasi, pelepasan, dan profil memakai jalur mod yang
sama. Pemeriksaan konten tidak membutuhkan impor ROM; data belum memengaruhi
gameplay dan belum memiliki validasi aturan gameplay tiap tipe.

Contoh paket tersedia di `examples/hello-crops/mod.json`:

```sh
cmake -E chdir examples/hello-crops cmake -E tar cf ../../build/hello-crops.zip --format=zip mod.json
./build/natureloader --data local-data --install build/hello-crops.zip
./build/natureloader --data local-data --enable hello-crops
./build/natureloader --data local-data --content
```

Manifest schema 1 sekarang menerima `texts`, `content`, atau keduanya. Minimal
satu harus berisi data. `content` adalah objek dengan ID `<mod-id>:<local-id>`;
namespace harus milik mod tersebut. Record memuat `type`, `name`, dan `data`
opsional berupa objek JSON. Tipe yang dikenali: `item`, `tool`, `npc`, `location`,
`dialog`, `schedule`, `story`, `building`, `sprite`, `ui_theme`, dan `rules`.
Batasnya 1024 record per mod, 64 KiB per record, serta batas paket yang sudah ada.
Ini validasi struktur untuk authoring, bukan janji bahwa field `data` sudah
menjadi perilaku engine. Referensi antarkonten, override lintas namespace, Lua,
dan migrasi save gameplay belum tersedia.

## Pemeriksaan tanpa GUI

```sh
./build/natureloader --data local-data --import "Harvest Moon - Back to Nature (USA).zip"
./build/natureloader --data local-data --install build/hello-text.zip
./build/natureloader --data local-data --enable hello-text
./build/natureloader --data local-data --text messages.promptYesNo
./build/natureloader --data local-data --disable hello-text
./build/natureloader --data local-data --status
./build/natureloader --data local-data --catalog-import "HM BTN IDN qol-ui-bin test-3.iso"
./build/natureloader --data local-data --catalog-list
```

Perintah CLI menghasilkan JSON dan tidak membutuhkan display server.
Impor ulang ke direktori data yang sudah berisi `base/` ditolak agar data
sebelumnya tidak ditimpa. Gunakan direktori data lain untuk mengimpor ulang.

Tes integrasi opsional memakai ROM lokal, tanpa mendistribusikannya:

```sh
cmake -S . -B build -DNATURE_TEST_ROM="$PWD/Harvest Moon - Back to Nature (USA).zip"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tes integrasi membutuhkan Python 3.11+ hanya untuk pengembangan. Tes memeriksa
SHA-256 ROM sumber dan semua file base sebelum/sesudah pergantian mod.

## Data dan provenance

ROM tidak ditulis ulang. Hasil impor berada di `base/`, mod di `mods/<id>/`,
dan urutan mod di `profile.json` dalam direktori data yang dipilih.
Pemeriksaan keaslian menggunakan hash executable dan bank teks yang diketahui;
hash arsip dicatat, tetapi tidak dibandingkan dengan daftar rilis independen.

Jangan memasukkan ZIP ROM, hasil impor, atau executable game ke paket aplikasi.
`.gitignore` memisahkan arsip lokal, `reference/`, `local-data/`, dan hasil build.
`.gitignore` bukan penyaring untuk ZIP distribusi yang dibuat manual.
Source referensi diekstrak dari ZIP milik pengguna ke `reference/` tanpa diedit.
Metadata bank teks dan pemetaan glyph berasal dari decomp tersebut; lihat
`THIRD_PARTY_NOTICES.md`. Lisensi decomp tidak melisensikan aset game atau SDK.

Lihat `docs/PORTING.md` untuk hambatan port dan urutan pengerjaan gameplay.
Lihat `docs/ROM_VARIANTS.md` untuk perbedaan ROM USA dan ROM mod Indonesia.
