# NatureLoader agent contract

Dokumen ini adalah titik masuk wajib untuk AI agent yang melanjutkan proyek.
Baca bersama `README.md`, `docs/PORTING.md`, dan `docs/NATIVE_PROGRESS.md`.

## Aturan utama

- Jawab dan dokumentasikan status secara jujur. Bedakan `authoring`, `snapshot/native export`, dan `runtime gameplay`.
- Jangan masukkan ROM, ISO, executable game, save, hasil ekstraksi, atau runtime riset ke Git.
- Jangan mengklaim fitur playable sebelum ada uji runtime yang dapat diulang.
- Pertahankan launcher Windows/Linux. Android tetap target berikutnya, bukan alasan memecahkan desktop.
- Modcore adalah engine internal dari satu modloader. Jangan membuat workflow atau tab modcore terpisah.
- Semua perubahan mod harus lewat satu storage: `mods/`, `profile.json`, resolver, dan native export.
- Perubahan migrasi harus idempotent, atomic, dan membuat backup sebelum menulis data lama.
- Jangan menulis ke disc sumber. Native patch harus bekerja pada snapshot atau runtime copy.
- Setiap fitur baru harus punya validator, test, dokumentasi batasan, dan completion gate.

## Peta kode

- `src/core.*`: facade launcher, paket mod schema 1/2, profile, CLI.
- `src/modcore_library.*`: backend unified mod lifecycle dan registry.
- `src/mod_migration.*`: migrasi `mods-v2/` dan profile lama.
- `src/native_mods.*`: native plan/export; saat ini teks dan field item retail terbatas.
- `src/rom_catalog.*`: import, hash, identifikasi, dan profile per ROM.
- `src/rom_panel.*`, `src/main.cpp`: UI launcher; jangan menambah workflow kedua.
- `modcore/`: resolver, registry, VFS, hook, script, save, plugin, C API.
- `tests/`: unit, integration, boundary, migration, dan smoke test.
- `examples/`: paket mod aman untuk authoring dan export.
- `docs/`: kontrak publik, bukti runtime, batasan, dan milestone.

## Alur perubahan

1. Baca dokumen ini dan cek `git status`.
2. Cari implementasi yang sudah ada sebelum membuat API baru.
3. Tetapkan slice kecil dan completion gate yang dapat diuji.
4. Implementasikan validator sebelum consumer runtime.
5. Tambahkan test regresi dan dokumentasikan bukti serta batasan.
6. Jalankan build/test yang relevan.
7. Perbarui `AGENTS.md`, `docs/AI_AGENT_GUIDE.md`, dan README bila arsitektur atau status publik berubah.
8. Tinjau file staged. Pastikan tidak ada data game.
9. Commit Conventional Commit. Push branch `main` memakai GitHub connector bila shell network tidak tersedia.

## Perintah Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake -S . -B build-modcore -DNATURE_BUILD_LAUNCHER=OFF
cmake --build build-modcore --parallel
```

Native runtime lokal memakai `NATURE_NATIVE_RUNTIME_DIRECTORY`; runtime tersebut tidak boleh dipublish.

## Prioritas teknis

Urutan kerja yang benar:

1. Sambungkan registry ke runtime game.
2. Implementasikan hook/script VM dengan sandbox dan failure isolation.
3. Tambahkan allocator/registry entity baru.
4. Sambungkan asset, map, NPC, event, collision, dan save/load.
5. Buktikan farm slice end-to-end.
6. Baru perluas sprite HD, item/tool/building/map baru, lalu packaging Windows/Android.

Saat ini fitur playable yang terbukti hanya subset teks UI dan patch field item retail native USA. Entity baru, sprite gameplay, area, hook, script, plugin, dan save native belum terhubung.

## GitHub handoff

Setelah perubahan lulus test, push commit ke `hndll56/Natureloader` branch `main`. Jika push biasa gagal karena network sandbox, gunakan GitHub connector API dengan lease pada ref terbaru. Verifikasi remote head, daftar file, dan pastikan artefak ROM/binary tidak ikut.

Setiap agent yang mengubah arsitektur wajib memperbarui dokumen ini dan `docs/AI_AGENT_GUIDE.md` pada commit yang sama.
