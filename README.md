# bq25792-utils

User-space C library + CLI for TI **BQ25792** (I2C) on Linux (e.g., Raspberry Pi CM5 / Debian Trixie).

What it gives you:

- `libbq25792.so` : C API to read charger status, ADC measurements, fault flags
- `bqctl`         : command-line tool with optional `--json` output (easy to consume from Python/Node/Go/etc.)

> Note: BQ25792 is a charger/power-path IC, not a fuel gauge. `soc_pct_est` is a **rough** estimate derived from pack voltage per cell.

## Build / Install (terminal)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libi2c-dev

git clone <YOUR_GITHUB_URL_HERE> bq25792-utils
cd bq25792-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
```

## Run

Defaults:
- bus: `BQ_I2C_BUS` env (default `10`)
- addr: `BQ_I2C_ADDR` env (default `0x6b`)

Examples:

```bash
bqctl status
bqctl status --json
bqctl --bus 10 --addr 0x6b status --json
bqctl raw
```

Example JSON:
```json
{
  "vbus_present":true,
  "pg":true,
  "chg_stat":3,
  "chg_stat_str":"Fast charge (CC)",
  "vbat_mv":3820,
  "ibat_ma":1200,
  "soc_pct_est":68,
  "fault_any":false
}
```

## Using from other languages

### Python (subprocess)

```python
import json, subprocess
out = subprocess.check_output(["bqctl","status","--json"], text=True)
st = json.loads(out)
print(st["soc_pct_est"], st["fault_any"])
```

### Node.js

```js
const {execFileSync} = require("child_process");
const st = JSON.parse(execFileSync("bqctl", ["status","--json"], {encoding:"utf8"}));
console.log(st.soc_pct_est, st.fault_any);
```
