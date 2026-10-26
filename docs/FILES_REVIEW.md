# Pemeriksaan files.zip

Arsip diperiksa pada 26 Oktober 2026. Isinya empat patch berurutan untuk
decomp matching yang dipakai sebagai referensi proyek ini. Patch diuji pada
salinan referensi di direktori sementara, bukan diterapkan pada launcher Qt.

## Manfaat

| Patch | Isi | Kegunaan untuk NatureLoader |
| --- | --- | --- |
| 0001 | Pemisah A_FILE berdasarkan HDT dan catatan progres | Membantu ekstraksi dan pemeriksaan anggota arsip. Katalog NatureLoader sekarang sudah membaca indeks dinamis, termasuk ROM IDN dengan 33 anggota. |
| 0002 | ModDb, schema, dependensi, merge field, registry ID, cache dan save | Bahan utama untuk mengembangkan mod konten melampaui penggantian teks. |
| 0003 | Generator salinan source yang kompatibel dengan compiler modern | Membantu jalur port berbasis decomp tanpa mengubah source matching. Lolos sintaks belum berarti berhasil menghasilkan executable PC. |
| 0004 | Sebelas tipe konten, referensi antarkonten, Lua 5.4.7 dan hook | Menyediakan kerangka NPC, lokasi, item, alat, bangunan, cerita, sprite dan aturan. Belum terhubung dengan engine BTN. |

Kumpulan ini menyediakan fondasi mod data yang lebih luas daripada format mod
teks launcher saat ini. Manfaat langsung pada desain launcher kecil: tidak
ada implementasi GUI atau proses menjalankan game di dalam arsip.

## Verifikasi yang dilakukan

- Keempat patch berhasil melewati `git apply --check` dan diterapkan berurutan
  pada salinan decomp referensi.
- Build Linux CMake/GCC berhasil, dengan 72 pemeriksaan `test_modcore` dan
  32 pemeriksaan `test_content` lulus.
- Empat tes Python pemisah arsip dan tiga tes generator kompatibilitas lulus.
- Generator kompatibilitas menghasilkan 435 prototipe dan satu perbaikan
  varargs. Sepuluh unity unit lolos `cc -fsyntax-only -std=gnu89` dengan header
  referensi. Ini tidak menguji link, ABI x64, inline assembly MIPS, GPU, input,
  audio, overlay atau gameplay.
- Tes Lua menggunakan `FakeApi`, bukan state game BTN yang sedang berjalan.

## Dua masalah yang dibuktikan

1. **Batas instruksi Lua dapat dilewati.** `port/modcore/script.cpp`, fungsi
   `countHook`, menghentikan eksekusi dengan `luaL_error`; sandbox masih
   mengekspos `pcall`. Handler yang berulang kali menjalankan loop melalui
   `pcall` tetap berjalan melewati anggaran 20000 instruksi sampai proses uji
   dihentikan oleh timeout eksternal dua detik. Perlu penghentian yang tidak
   bisa dipulihkan script, atau isolasi proses dengan batas waktu host.
2. **Cache yang rusak dapat menjatuhkan host.** `port/modcore/moddb.cpp`,
   fungsi `restore`, mengakses hasil `root.find("mods")` dan field lain tanpa
   memeriksa keberadaan/tipe. Cache dengan header/hash valid tetapi root biner
   kosong menghasilkan SIGSEGV (exit -11) pada probe Linux. `catch` exception
   C++ tidak menangkap dereference null tersebut. Seluruh struktur cache harus
   divalidasi sebelum state aktif diubah, lalu cache gagal harus dibangun ulang.

Probe tambahan ini dijalankan terpisah dari tes bawaan. File ZIP dan source
launcher tidak diubah oleh pemeriksaan. Temuan bukan audit keamanan lengkap.

## Batas dan integrasi yang tepat

Schema menerima lokasi besar dan konten baru pada tingkat data; hal itu belum
menghilangkan batas tabel, memori, save dan perilaku gameplay di kode asli.
Nilai batas schema sendiri ditandai sebagai dugaan oleh dokumentasi patch.

Format manifest juga berbeda: patch memakai folder mod, ID dengan underscore,
`depends/load_after`, dan operasi data; launcher saat ini memakai ZIP dengan
`schema`, `game`, ID bertanda hubung, dan override teks. Keduanya membutuhkan
adapter atau satu format yang disepakati, bukan diterapkan bersamaan tanpa
aturan versi. Parser JSON baru tidak perlu diduplikasi untuk fungsi yang sudah
dicakup Qt JSON. Build `port/` memilih C++20 dan flag GCC; build Windows/MSVC
masih perlu diverifikasi dan flag compiler perlu dipisahkan.

Urutan penggunaan yang disarankan: ambil spesifikasi konten dan aturan ID,
perbaiki cache/Lua, satukan format paket dan profil ROM, lalu hubungkan registry
dan hook ke fungsi game native yang telah terbukti berjalan. Tetap pertahankan
jalur matching decomp sebagai referensi terpisah. Patch ini tidak menyediakan
source overlay yang hilang atau native runtime lengkap, dan tidak membuat ROM
mod Indonesia kompatibel dengan build USA secara otomatis.
