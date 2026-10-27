# Mod nilai item retail

NatureLoader menerjemahkan `$patch` schema 2 ke empat field pada tabel item USA:

| Field | Tipe/batas | Makna |
| --- | --- | --- |
| `buy_price` | s16, -32768..32767 | Nilai harga beli di tabel retail. |
| `sell_price` | s16, -32768..32767 | Nilai harga jual di tabel retail. |
| `stamina` | s8, -128..127 | Nilai stamina sesuai perilaku item asli. |
| `fatigue` | s8, -128..127 | Nilai fatigue sesuai perilaku item asli. |

Ini mengubah item yang sudah ada. Penambahan ID baru, penggantian callback, sprite,
crop, atau inventori belum dihubungkan ke runtime. Makna nilai negatif mengikuti
kode retail; validasi lebar angka tidak menjamin setiap kombinasi cocok untuk gameplay.

## Contoh

```json
{
  "schema": 2,
  "id": "lobak-mahal",
  "name": "Lobak Mahal",
  "version": "1.0.0",
  "game": "SLUS_011.15",
  "api": "^1.0",
  "content": {
    "$patch": {"base:turnip": {"sell_price": 120, "stamina": 6}}
  }
}
```

Kemas `mod.json` di root ZIP, pasang lewat **Peti mod**, lalu aktifkan. Contoh
lebih lengkap tersedia di `examples/native-balance-v2/mod.json`. Mod teks dan mod
item berbagi profil dan urutan resolver yang sama. Field yang ditimpa lebih dari
satu mod mendapat satu nilai akhir dan satu patch RAM; laporan konflik menunjukkan
mod yang menimpanya. Menonaktifkan mod dan menjalankan ulang memakai snapshot baru.

Nama item berasal dari `modcore/data/base_catalog.json`. Hanya indeks tabel
`0..379` yang didukung. ID cooking-only seperti `base:sugar` berada di luar tabel
tersebut dan ditolak. Field lain, entitas baru, script, aset, hook, serta plugin
aktif belum didukung saat boot native. Paket-paket tersebut tetap bisa diperiksa
sebagai authoring, tetapi tidak diabaikan diam-diam saat ekspor.

## Batas teknis dan bukti

Exporter memerlukan executable dengan SHA-256 USA yang dikenal. Tabel `ItemInfo`
ada pada RAM `0x8004B1A4`, dengan 380 baris berukuran 40 byte. Offset harga beli,
harga jual, stamina, dan fatigue adalah 16, 18, 32, dan 33. Pemetaan diperiksa
dari header tipe/simbol decomp serta executable lokal. Alamat tersebut disimpan
di adapter game; pembuat mod cukup memakai ID dan nama field.

Setiap patch membawa byte asli dari executable milik pemain. Runtime memeriksa
semua byte yang diharapkan sebelum menulis rencana ke RAM. ROM dan executable
sumber tidak ditulis ulang. `src/native_mods.cpp` membaca batas segmen dari header
PS-X EXE dan menolak rentang di luar data yang dimuat.

Tes integrasi dengan ROM USA membuktikan nilai bertanda, alamat, byte pengaman,
campuran mod teks/item, dan hilangnya patch setelah mod dinonaktifkan. Hash ROM
serta data base tidak berubah. Ini belum membuktikan transaksi jual atau aksi
bertani memakai nilai tersebut.

The corrected live audit on 2026-10-27 passed stock/modded/restored table reads
in three native processes, with advancing frames, zero interpreted instructions,
and an unchanged source executable. The earlier BIOS-stop conclusion came from
a faulty persistent debugger connection and intentional test cleanup. See
[the native audit](NATIVE_PROGRESS.md) for the command and exact scope.
