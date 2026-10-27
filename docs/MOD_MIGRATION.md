# Penggabungan modloader

NatureLoader sekarang memakai satu tab **Peti mod**, satu tombol pemasangan ZIP,
folder `mods/`, dan `profile.json`. Modcore menjadi engine internal untuk
dependensi, versi, urutan, validasi, registry, serta rencana mod native. Format
schema 1 dan 2 berbagi jalur tersebut. Flag CLI `--v2-*` tetap tersedia sebagai
alias kompatibilitas; tidak membuat profil terpisah.

## Data lama

Saat folder data lama dibuka, loader menggabungkan urutan profil lama kemudian
profil v2. Resolver tetap mendahulukan dependensi. Paket di `mods-v2/` disalin ke
`mods/`, tanpa mengubah paket sumber. `authoringSave` dari profil v2 dipertahankan
agar handle, flag, dan data mod tidak hilang.

Sebelum commit, profil asli disalin ke `mod-migration-backups/<id>/`.
`profile.json` baru mencatat lokasi tersebut pada `migrationBackup`. Folder
`mods-v2/` dan `profile-v2.json` asli tetap ada sebagai sumber pemulihan, tetapi
tidak lagi digunakan untuk aktivasi. Marker `unified` mencegah impor ulang pada
peluncuran berikutnya.

ID yang sama dengan isi berbeda menghentikan migrasi. Tidak ada paket yang
ditimpa. Semua paket baru disiapkan dahulu; jika commit profil gagal, salinan
yang baru dipindahkan dikembalikan ke staging. Profil ditulis atomik dan dibatasi
2 MiB agar tetap dapat dibaca kembali.

## Pemulihan

Tutup launcher sebelum memulihkan data. Kerjakan pemulihan pada salinan folder
data agar kondisi sekarang tetap tersedia. Profil sebelum migrasi ada di lokasi
`migrationBackup`; paket v2 aslinya tetap berada di `mods-v2/`. Untuk memakai
launcher lama, pulihkan profil lamanya dan gunakan kembali paket sesuai format
yang didukung launcher tersebut. Launcher schema 1 lama tidak bisa membaca paket
schema 2 yang sekarang ada di folder gabungan `mods/`; pindahkan paket schema 2
dari folder `mods/` pada salinan pemulihan tersebut.

Jangan menjalankan launcher lama dan baru terhadap folder data yang sama secara
bersamaan. Loader baru tidak menyinkronkan perubahan yang dibuat launcher lama
setelah penggabungan. Tes migrasi mencakup cadangan byte-identik, pengulangan,
konflik ID tanpa overwrite, dependensi lintas format, dan pemetaan ID yang tetap.
