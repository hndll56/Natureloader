# Format mod konten eksperimental

Dokumen ini menjelaskan schema 1. Schema 2 tersedia melalui tab **Modcore v2**;
lihat [API v2](MOD_API.md) untuk dependensi, registry, dan profil authoring-nya.
Kedua format masih belum menambahkan entitas baru ke gameplay BTN.

Registry ini membantu memasang dan memeriksa data mod melalui launcher. Registry
belum terhubung ke gameplay BTN. Record `ui_theme` belum mengubah UI game,
record `item` belum menambah item yang dapat dipakai, dan record `sprite` belum
mengganti tekstur renderer. Status API tetap mengembalikan
`contentGameplayReady: false`.

## Paket dan manifest

Paket adalah ZIP dengan `mod.json` di root. `README.md` dan `LICENSE` opsional;
file lain, subfolder, symlink, hardlink, dan nama duplikat ditolak. Batas ukuran
adalah 1 MiB per file dan 2 MiB untuk total data setelah dekompresi. Paket baru
dipasang nonaktif. ID yang sudah terpasang tidak ditimpa.

Contoh minimum:

```json
{
  "schema": 1,
  "game": "SLUS_011.15",
  "id": "hello-crops",
  "name": "Kebun Percobaan",
  "version": "1.0.0",
  "content": {
    "hello-crops:blue-turnip": {
      "type": "item",
      "name": "Lobak Biru",
      "data": {"sellPrice": 120}
    }
  }
}
```

Field manifest yang diizinkan hanya `schema`, `game`, `id`, `name`, `version`,
`texts`, dan `content`. `schema` harus angka 1 dan `game` harus string
`SLUS_011.15`. Nama mod berupa string tidak kosong setelah whitespace dipangkas,
maksimal 120 unit UTF-16. Versi berupa string tidak kosong, maksimal 40 unit
UTF-16; saat ini versi tidak ditafsirkan sebagai semantic version.

ID mod terdiri dari 1–64 karakter: karakter pertama huruf kecil ASCII, sisanya
huruf kecil ASCII, digit, atau tanda hubung. Nama perangkat Windows seperti
`con` dan `nul` ditolak untuk direktori mod. Whitespace, termasuk newline di
akhir ID, ditolak.

`texts` dan `content` opsional, tetapi minimal satu harus berupa objek berisi
data. Jika disertakan, keduanya harus objek JSON. Mod teks lama tetap berlaku;
mod juga dapat memuat teks dan konten bersama. Validasi teks tetap memakai
daftar kunci dari `resources/text_bank.layout.json` dan batas 16384 unit UTF-16
per string pengganti.

## Record konten

Kunci record berbentuk `<mod-id>:<local-id>`. Namespace harus sama persis dengan
ID mod pemilik. `local-id` mengikuti aturan huruf kecil/digit/tanda hubung dengan
panjang 1–64 karakter. Dua mod dapat menggunakan local ID sama, karena namespace
berbeda. Mod tidak boleh menambahkan atau mengganti record milik namespace lain.

Setiap record adalah objek dengan field berikut saja:

| Field | Aturan |
| --- | --- |
| `type` | Wajib, salah satu tipe yang tercantum di bawah. |
| `name` | Wajib, string tidak kosong setelah pemangkasan, maksimal 120 unit UTF-16. |
| `data` | Opsional, objek JSON yang dipertahankan sebagai data authoring. |

Tipe yang dikenali: `item`, `tool`, `npc`, `location`, `dialog`, `schedule`,
`story`, `building`, `sprite`, `ui_theme`, dan `rules`. Maksimal 1024 record per
mod, maksimal 65536 byte per record setelah serialisasi JSON compact oleh Qt.
Batas keseluruhan file dan paket tetap berlaku.

Field di dalam `data` belum memiliki schema khusus tiap tipe. Contohnya,
`sellPrice` disimpan tetapi belum divalidasi sebagai harga game dan belum
dibaca oleh gameplay. Referensi antarkonten juga belum divalidasi. Lua, DLL,
dependensi antarmod, penggantian record lintas namespace, dan migrasi save game
belum didukung.

## Aktivasi dan pemeriksaan

Instal ZIP melalui tab **Peti mod USA**, lalu aktifkan mod. Tab **Konten mod**
menampilkan record aktif dan JSON aslinya. Registry dapat diperiksa tanpa ROM:

```sh
./build/natureloader --data local-data --install build/hello-crops.zip
./build/natureloader --data local-data --enable hello-crops
./build/natureloader --data local-data --content
./build/natureloader --data local-data --disable hello-crops
```

Aktivasi tersimpan dalam profil. Menonaktifkan mod mengeluarkan recordnya dari
registry; mengaktifkannya kembali memulihkan data. Pelepasan memindahkan paket
terpasang ke `removed-mods/` dan menghapusnya dari profil aktif. Mengubah urutan
mod tidak mengganti record konten mod lain karena namespace harus terpisah.
Prioritas terakhir menang tetap berlaku untuk penggantian teks yang berbagi
kunci game.

`tests/core_test.cpp` memeriksa siklus instalasi/aktivasi/persistensi/pelepasan,
isolasi namespace, penolakan tipe dan struktur invalid, batas ukuran/jumlah,
serta ID/path dengan newline. Keberhasilan tes ini membuktikan registry dan
validasinya, bukan modifikasi gameplay.

Runtime native menolak mod `content` yang aktif karena record tersebut belum
terhubung ke gameplay. Mod UI yang benar-benar bekerja saat ini memakai field
`texts` dan exporter teks native.
