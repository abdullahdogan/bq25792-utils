# bq25792-utils

Raspberry Pi CM5 (Debian Trixie) üzerinde TI **BQ25792** şarj/power-path entegresini I2C ile okuyup;

- **bqctl**: CLI ile canlı durum/ADC okumaları
- **bq25792d**: her **10 saniyede 1** ölçüm alıp `/run/bq25792/status.json` dosyasına **cache** yazan daemon
- `libbq25792.so`: C kütüphanesi (diğer uygulamalar/SDK’lar için)

sağlar.

> Not: BQ25792 bir **fuel-gauge** değildir. Repo içindeki SoC `%` değeri **VBAT (hücre başına voltaj) üzerinden kaba tahmin**dir. Kesin yüzde için harici fuel-gauge önerilir.

---

## İçerik

- `src/bqctl.c` → CLI aracı
- `src/bq25792d.c` → cache daemon
- `src/bq25792.c`, `include/bq25792.h` → kütüphane
- `systemd/bq25792d.service` → systemd servisi
- `install.sh` → kurulum/güncelleme scripti

---

## Gereksinimler

- Debian/Raspberry Pi OS (Trixie) + **systemd**
- I2C aktif olmalı (ör. `/dev/i2c-10` mevcut)
- Paketler:
  - `build-essential`, `cmake`, `pkg-config`, `libi2c-dev`

`install.sh` bunları otomatik kurar.

---

## Kurulum

```bash
git clone https://github.com/abdullahdogan/bq25792-utils.git
cd bq25792-utils
chmod +x install.sh
sudo ./install.sh
```
