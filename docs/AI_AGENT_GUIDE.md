# NatureLoader: AI agent handoff guide

Panduan ini menjelaskan cara agent baru memahami, mengubah, menguji, dan meneruskan NatureLoader tanpa mengulang investigasi lama atau membuat klaim fitur yang belum terbukti.

## Produk

NatureLoader adalah launcher desktop dan fondasi port native Harvest Moon: Back to Nature. Pemain memasukkan disc/ROM miliknya sendiri. Launcher mengimpor dan meng-hash sumber secara read-only, menyimpan profile per ROM, lalu mengaktifkan paket mod tanpa mengubah disc asli.

Proyek ini bukan emulator dan belum menjadi remake penuh. Runtime native masih bertahap. Modcore adalah library internal yang dipakai unified launcher, bukan produk atau tab terpisah.

## Arsitektur data

```text
ROM milik pemain (read-only)
  -> rom_catalog + hash + profile per ROM
  -> mods/<id>/ + profile.json
  -> ModcoreLibrary resolver
      -> text/native plan
      -> content registry preview
      -> future runtime hooks/assets/save
  -> native runtime copy
```

Schema 1 dan schema 2 memakai installer, folder, profile, load order, conflict report, text viewer, dan native exporter yang sama. Alias CLI `--v2-*` hanya compatibility alias.

## Batas dukungan saat ini

- Native export: text UI tertentu dan `buy_price`, `sell_price`, `stamina`, `fatigue` pada 380 base item retail USA.
- Registry authoring: item, NPC, lokasi, bangunan, alat, asset, hook, script, dan metadata dapat divalidasi/ditampilkan.
- Belum playable: entity baru, sprite HD, area baru, building baru, tool baru, event baru, Lua, plugin gameplay, dan save extension native.
- Boot/title/menu Memory Card pernah diuji pada runtime riset. Farm gameplay, save/reload, Windows, dan Android belum menjadi bukti release.

Jika pekerjaan menyentuh area belum terhubung, implementasikan fail-closed: registry boleh menyimpan data, tetapi exporter harus menolak dengan error jelas sampai runtime consumer tersedia.

## Konvensi pengembangan

Gunakan C++17, CMake, Qt 6 Widgets, libarchive, dan library modcore tanpa dependensi baru kecuali ada alasan kuat. Pertahankan nama API publik dan compatibility wrapper bila migration belum selesai. Validasi input sebelum mutasi. Untuk file/profile gunakan staging, atomic rename, batas ukuran, dan backup.

Native patch wajib:

1. Memastikan executable/hash target cocok.
2. Memastikan alamat dan ukuran berada dalam load image.
3. Memeriksa tipe, rentang, kapasitas, dan konflik.
4. Menghasilkan patch pada copy/snapshot runtime.
5. Membiarkan source disc tetap byte-identical.

## Cara memilih pekerjaan

Mulai dari gap yang paling dekat dengan runtime. Untuk konten baru, urutan minimal adalah:

1. logical ID dan schema;
2. registry allocation;
3. asset/VFS lookup;
4. runtime consumer dan hook;
5. save ownership/missing-mod behavior;
6. test add, load, save, reload, disable;
7. dokumentasi dan compatibility version.

Jangan mulai dari editor besar atau plugin API sebelum satu konten kecil berhasil melewati urutan tersebut.

## Validasi sebelum commit

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tambahkan `NATURE_TEST_ROM` hanya dengan path lokal yang tidak masuk commit. Untuk perubahan modcore, gunakan build Qt-off. Untuk perubahan launcher, jalankan `launcher_smoke`. Catat test yang tidak dapat dijalankan dan alasannya.

## Dokumentasi wajib

Perbarui dokumen yang relevan ketika mengubah perilaku:

- format/mod workflow: `docs/MOD_API.md`, `docs/MEMBUAT_MOD.md`, `docs/MOD_CONTENT.md`;
- native patch: `docs/NATIVE_ITEM_MODS.md`, `docs/NATIVE_PROGRESS.md`;
- runtime/decomp: `docs/PORTING.md`, `docs/DECOMP_INTEGRATION.md`;
- migration/storage: `docs/MOD_MIGRATION.md`;
- arsitektur agent: `AGENTS.md` dan dokumen ini.

Dokumentasi harus menyebut bukti test, target ROM/runtime, dan batas yang masih ada. Jangan mengubah kata “preview” menjadi “playable” tanpa bukti runtime.

## Handoff dan push

Sebelum push:

```sh
git status --short
git diff --check
```

Commit hanya source, tests, docs, examples, licenses, dan konfigurasi build. Tolak ROM/ISO/BIN/EXE/DLL/SO, save, `reference/`, `local-data/`, dan build output. Push ke `main`, lalu verifikasi commit remote dan isi tree melalui GitHub connector. Laporkan hash commit, test, dan batas fitur secara singkat.

## Known next milestones

Milestone berikutnya bukan menambah metadata saja. Target nyata adalah farm slice native: new game, movement, map transition, satu farming action, sleep/day advance, save/reload. Setelah itu sambungkan satu sprite override gameplay, lalu satu entity baru dengan deterministic save ID. Semua milestone harus memiliki completion gate yang bisa direproduksi.
