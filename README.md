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
```

## Daemon (10 sn’de 1 guncelleme)

Servisi etkinleştir:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now bq25792d.service
```

Kontrol:

```bash
systemctl status bq25792d.service
cat /run/bq25792/status.json
```

### Konfigürasyon

Varsayılanlar:
- `BQ_I2C_BUS=10`
- `BQ_I2C_ADDR=0x6b`
- `BQ_INTERVAL_SEC=10`
- `BQ_STATUS_PATH=/run/bq25792/status.json`

Override:

```bash
sudo systemctl edit bq25792d.service
```

Örnek:
```ini
[Service]
Environment=BQ_I2C_BUS=10
Environment=BQ_I2C_ADDR=0x6b
Environment=BQ_INTERVAL_SEC=10
```

Sonra:
```bash
sudo systemctl restart bq25792d.service
```

## 3. taraf uygulamalar nasil okuyacak?

**Önerilen (stabil yüzde):**
- Dosyadan: `/run/bq25792/status.json`
- veya komutla: `bqctl cached --json`

Örnek Python:
```python
import json, subprocess
st = json.loads(subprocess.check_output(["bqctl","cached","--json"], text=True))
print(st["soc_pct"], st["fault_any"], st["chg_stat_str"])
```

Örnek Node.js:
```js
const {execFileSync} = require("child_process");
const st = JSON.parse(execFileSync("bqctl", ["cached","--json"], {encoding:"utf8"}));
console.log(st.soc_pct, st.fault_any, st.chg_stat_str);
```

## JSON alanlari (özet)

- `soc_pct` : stabilize edilmiş yüzde (3. taraf için)
- `soc_raw` : anlık (kaba) yüzde tahmini
- `soc_filt`: filtrelenmiş float
- `fault_any`, `fault0`, `fault1` : hata bilgileri
- `chg_stat_str`, `vbus_stat_str` : okunabilir durum metinleri
- `vbat_mv`, `ibat_ma`, `vbus_mv` ... : ADC ölçümleri

## Stabilizasyon mantigi (kisa)

- Ölçüm 10 sn’de 1 alınır.
- Yüzde için EMA (low-pass) + **yön bazlı anti-jitter** uygulanır:
  - Şarj olurken küçük düşüşler, deşarj olurken küçük yükselişler bastırılır.
- Rate limit: varsayılan **en fazla 1%/dakika** değişim (UI dalgalanması önlenir).

İsterseniz cihazınızın gerçek karakteristiğine göre bu parametreleri (alpha, eşik, rate-limit) ayarlayabiliriz.
