# bq25792-utils

TI **BQ25792** için Linux (Debian Trixie / Raspberry Pi CM5) üzerinde çalışan **user-space C kütüphanesi + CLI + cache daemon**.

Hedefler:
- I2C üzerinden BQ25792 register’larını okuyup **tek bir “durum” çıktısı** üretmek
- Ölçümü **her 10 saniyede 1** güncellemek
- 3. taraf uygulamalar okurken **şarj yüzdesinde dalgalanma görmemesini** sağlamak (stabilize edilmiş yüzde)

> Önemli: BQ25792 bir şarj/power-path entegresidir, **fuel-gauge değildir**. Bu repodaki yüzde (`soc_*`) değerleri **VBAT (hücre başına voltaj) üzerinden kaba bir tahmindir**. Daha doğru yüzde için harici fuel gauge önerilir.

## Bileşenler

- `libbq25792.so` : C API (charger durum + ADC ölçümleri + fault flag)
- `bqctl`         : komut satırı aracı  
  - `bqctl status --json` → CANLI okuma (anlık)
  - `bqctl cached --json` → cache dosyasını okur (stabil)
- `bq25792d`      : daemon (varsayılan **10 sn** aralıkla `/run/bq25792/status.json` üretir)
- `systemd/bq25792d.service` : boot’ta otomatik başlatma

## Kurulum

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libi2c-dev

git clone <GITHUB_REPO_URL> bq25792-utils
cd bq25792-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
