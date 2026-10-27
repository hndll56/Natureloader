# NatureLoader

**Launcher desktop dan fondasi port native PC untuk Harvest Moon: Back to Nature (BTN).** NatureLoader dirancang agar pemain memakai disc sendiri, mengelola beberapa versi ROM/mod secara terpisah, lalu memasang atau melepas mod dari launcher tanpa menimpa disc sumber.

> **Status: eksperimental.** Launcher dan sistem katalog/mod berjalan di Linux. Jalur native riset sudah menjalankan boot, title, dan menu Memory Card untuk executable USA yang didukung. Gameplay farm lengkap, save/reload, Windows, dan ROM hack dengan executable berbeda belum diverifikasi.

## Tujuan proyek

- Memudahkan pengelolaan ROM milik pemain dan ROM hack sebagai entri terpisah.
- Menyediakan format mod modular yang bisa dipasang, diaktifkan, dinonaktifkan, dan ditukar dari launcher.
- Membangun port native Windows/Linux; Android menjadi target setelah runtime desktop lebih matang.
- Membuka jalan menuju mod konten besar melalui engine dan registry yang bisa diperluas.

NatureLoader **bukan emulator** dan tidak membangun ulang seluruh game dari nol. Port native menjalankan kode game yang telah direkonstruksi/dipadankan dengan backend PC. Namun, banyak kode asli BTN tersimpan sebagai overlay MIPS di arsip disc dan tidak tersedia sebagai source portabel. Karena itu, port gameplay harus dilakukan bertahap.

## Cara kerja

```text
Disc/ROM milik pemain (read-only)
          │
          ├── katalog, hash, profil per ROM, pembaca ISO/arsip
          │
          ├── paket mod ZIP → validasi manifest → urutan mod aktif
          │                                  ├── penggantian teks → RAM runtime native
          │                                  └── registry konten → pratinjau launcher
          │
          └── runtime native riset → game di jendela PC
```

Launcher berbasis C++17, Qt 6 Widgets, dan libarchive. Logika impor, katalog ROM, validasi mod, serta penyimpanan profil ada di `src/core.*`, `src/rom_catalog.*`, dan `src/native_mods.*`. Runtime riset native bersifat checkout development opsional; source tersebut tidak otomatis diunduh atau dibundel.

Data ROM tetap di direktori data lokal. Profil dipisahkan per hash ROM agar mod dan save untuk satu versi tidak tercampur dengan versi lain. Mod aktif dengan prioritas tertinggi menang untuk key teks yang sama. ROM sumber tidak dipatch.

## Fitur saat ini

- Impor BIN/ISO, BIN Mode 2/2352, ISO sektor 2048 byte, CUE satu track tertentu, dan ZIP berisi satu disc.
- Katalog ROM dan ROM hack, identifikasi SHA-256, pilihan versi persisten, serta profil terpisah.
- Pembacaan ISO9660 dan ekstraksi file game yang diperlukan untuk alat inspeksi.
- Pembacaan bank teks; penampil teks native dan penggantian teks melalui mod.
- Instalasi paket ZIP, aktivasi/nonaktivasi, urutan prioritas, laporan konflik, serta pelepasan mod yang bisa dipulihkan.
- Registry konten eksperimental untuk mengemas dan melihat data item, NPC, lokasi, bangunan, alat, dan tipe lain.
- Decoder awal untuk kontainer tekstur 0D dan ekspor PNG untuk pemeriksaan aset.
- Tombol menjalankan runtime native lokal untuk executable USA yang sudah diverifikasi.

## Status modding

Tabel ini menjelaskan dukungan launcher dan runtime BTN saat ini. Modcore v2 terpisah dijelaskan setelah tabel.

| Jenis perubahan | Status |
| --- | --- |
| Teks UI tertentu | Bisa diterapkan ke RAM runtime native USA yang didukung; glyph dan kapasitas masih mengikuti batas retail. |
| Sprite HD / penggantian sprite | Belum terhubung ke renderer gameplay. |
| Tema atau layout UI | Belum diterapkan ke UI game. |
| Item, alat, NPC, lokasi, bangunan, jadwal, cerita | Bisa disimpan dan diperiksa sebagai registry JSON; belum memengaruhi gameplay. |
| Lua, plugin kode, konten lintas mod | Belum didukung. |
| ROM hack dengan executable berbeda | Bisa dikatalogkan; belum tentu bisa dijalankan native. |

Jadi, registry konten **belum** berarti konten baru bisa dimainkan. Lihat [kemajuan port](docs/PORTING.md), [status native](docs/NATIVE_PROGRESS.md), dan [format konten eksperimental](docs/MOD_CONTENT.md) untuk detail dan batas yang diketahui.

## Fondasi modding v2 eksperimental

Paket lokal `Natureloader-v2-modcore.zip` menyediakan fondasi tambahan yang kini dibangun sebagai library `nature_modcore` dan CLI `naturemod`. Modul ini mencakup resolver dependensi/versi, registry ID, aset dan override melalui VFS, hook dan script deklaratif, ekstensi data save, serta C ABI untuk disambungkan ke core game.

**Tab Modcore v2 kini memakai library ini** untuk pemasangan ZIP, aktivasi, validasi dependensi, urutan efektif, dan pemeriksaan registry. Profil v2 menyimpan pemetaan ID authoring secara persisten. Mod teks v2 memakai jalur ekspor RAM native yang sama dengan mod teks lama. Contoh item, NPC, area, toko, dan sprite HD di `examples/v2/` masih membuktikan kontrak lewat core tiruan; file map contoh adalah placeholder. Konten tersebut belum bisa dimainkan di BTN.

Untuk memeriksa mod schema 2 tanpa Qt, ROM, atau runtime game:

```sh
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-modcore --parallel
./build-modcore/naturemod check examples/v2
./build-modcore/naturemod list examples/v2
ctest --test-dir build-modcore --output-on-failure
```

Lihat [API mod v2](docs/MOD_API.md), [arsitektur v2](docs/ARCHITECTURE_V2.md), dan [titik integrasi decomp](docs/DECOMP_INTEGRATION.md). ABI dan format masih eksperimental. Rentang ID beberapa tipe masih perkiraan dan perlu disesuaikan dengan gameplay asli.

Contoh teks UI v2 yang memakai jalur native:

```sh
cmake -E chdir examples/native-ui-v2 cmake -E tar cf ../../build/native-ui-v2.zip --format=zip mod.json
./build/natureloader --data local-data --v2-install build/native-ui-v2.zip
./build/natureloader --data local-data --v2-enable native-ui-v2
./build/natureloader --data local-data --v2-status
```

Pasang ZIP schema 2 lewat tab **Modcore v2**. Mod yang membutuhkan dependensi baru dapat dipasang nonaktif; aktivasi ditolak sampai dependensi dan seluruh paket valid. Mod konten/script/aset aktif masih memblokir boot BTN agar fitur yang belum didukung tidak diabaikan. Key teks yang muncul pada profil v1 dan v2 sekaligus harus diselesaikan dengan menonaktifkan salah satu mod.

## Memasang mod

Mod saat ini berupa ZIP yang berisi `mod.json` di root. Lihat [panduan membuat mod](docs/MEMBUAT_MOD.md) dan contoh pada `examples/`.

Contoh mod teks minimal:

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

Untuk membuat ZIP contoh dari root proyek:

```sh
cmake -E chdir examples/native-ui cmake -E tar cf ../../build/native-ui.zip --format=zip mod.json
```

Pasang ZIP lewat **Pasang mod ZIP**, lalu aktifkan mod. Jalankan native USA yang didukung dan buka **Continue** di title untuk melihat teks UI berubah. Nonaktifkan mod dan jalankan ulang untuk kembali ke teks asli. Mod UI yang bekerja saat ini ditunjukkan oleh `examples/native-ui/mod.json`.

Mod konten: mulai dari `examples/hello-crops/mod.json`, lalu ikuti [spesifikasi registry](docs/MOD_CONTENT.md). ZIP perlu memuat `mod.json` di root; schema `1` dan `game: "SLUS_011.15"` wajib. Mod konten tetap hanya pratinjau selama belum ada dukungan gameplay.

## Build dan jalankan

### Linux

Perlu compiler C++17, CMake 3.21+, Qt 6 Widgets, dan libarchive development headers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/natureloader
```

Untuk menyimpan data proyek di folder lokal:

```sh
./build/natureloader --data local-data
```

### Windows

Perlu Visual Studio 2022 Desktop C++, CMake, Qt 6 MSVC x64, dan libarchive x64 melalui vcpkg. Jalankan di Developer PowerShell; sesuaikan `QT_ROOT` dan `VCPKG_ROOT`:

```powershell
vcpkg install libarchive:x64-windows
cmake -S . -B build-win -A x64 `
  -DCMAKE_PREFIX_PATH="$env:QT_ROOT" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-win --config Release --parallel
& "$env:QT_ROOT/bin/windeployqt.exe" build-win/Release/natureloader.exe
ctest --test-dir build-win -C Release --output-on-failure
```

Build Windows dan paket installer belum diuji pada mesin Windows asli. Distribusi perlu menyertakan Qt, libarchive, plugin platform, dan notice lisensi yang sesuai.

### Menjalankan runtime native riset

Runtime native tidak dibundel. Untuk pengembangan, siapkan checkout lokal kompatibel pada `reference/btn-recomp`, lalu konfigurasi:

```sh
cmake -S . -B build -DNATURE_NATIVE_RUNTIME_DIRECTORY="$PWD/reference/btn-recomp"
cmake --build build --parallel 2
```

Impor disc USA milik sendiri di **Koleksi ROM**, pilih versi tersebut, lalu tekan **Jalankan native (uji)**. Tanpa checkout runtime lokal, tombol ini tidak tersedia. SHA-256 executable diperiksa sebelum start. ROM hack dengan executable berbeda belum didukung oleh jalur ini.

## Dokumentasi

- [Membuat mod](docs/MEMBUAT_MOD.md) — paket teks dan alur instalasi.
- [Konten mod](docs/MOD_CONTENT.md) — schema registry eksperimental dan keterbatasannya.
- [API mod v2](docs/MOD_API.md) — format folder mod dan CLI pemeriksa.
- [Arsitektur v2](docs/ARCHITECTURE_V2.md) — registry, dependensi, VFS, hook, dan save.
- [Integrasi decomp](docs/DECOMP_INTEGRATION.md) — sambungan yang masih perlu dikerjakan pada core game.
- [Kemajuan port](docs/PORTING.md) — arsitektur, temuan decomp, dan milestone gameplay.
- [Status native](docs/NATIVE_PROGRESS.md) — catatan runtime dan bukti yang tersedia.
- [Varian ROM](docs/ROM_VARIANTS.md) — USA, ROM hack, dan kompatibilitas.
- [Tinjauan file](docs/FILES_REVIEW.md) — provenance dan pemilahan source.
- [Third-party notices](THIRD_PARTY_NOTICES.md) — sumber kode dan lisensi.

## Privasi, ROM, dan lisensi

Jangan unggah ROM, executable game, file hasil ekstraksi, atau save pribadi ke repo/paket distribusi. `.gitignore` mengecualikan ekstensi ROM umum, folder build, `reference/`, dan `local-data/`; tinjau isi paket sebelum membagikan.

Source launcher NatureLoader dan aset game adalah hal berbeda. Lisensi decomp tidak memberi hak atas game, musik, gambar, atau ROM BTN. Runtime riset yang disebut dalam dokumentasi memiliki batas lisensi tersendiri; periksa [notices](THIRD_PARTY_NOTICES.md) sebelum redistribusi atau penggunaan komersial.

## Kontribusi

Perubahan yang membantu port dapat direproduksi, menjaga data ROM tetap lokal, dan memperjelas status dukungan mod sangat berguna. Sebelum menambah tipe konten, jelaskan schema, validasi, kompatibilitas save, dan bagian runtime yang akan mengonsumsinya. Hindari klaim bahwa konten bisa dimainkan sebelum perubahan tersebut diuji di runtime.
