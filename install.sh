#!/usr/bin/env bash
set -euo pipefail

# bq25792-utils install script (idempotent)
# - builds & installs binaries/libs
# - installs/updates systemd service
# - enables & starts bq25792d at boot

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "Bu script root ile calismali. Ornek:"
  echo "  sudo ./install.sh"
  exit 1
fi

echo "[1/6] Paketler yukleniyor..."
apt-get update -y
apt-get install -y git build-essential cmake pkg-config libi2c-dev

echo "[2/6] Build..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "[3/6] Install..."
cmake --install build
ldconfig

echo "[4/6] systemd servis dosyasi yukleniyor..."
UNIT_SRC="${SCRIPT_DIR}/systemd/bq25792d.service"
UNIT_DST="/etc/systemd/system/bq25792d.service"

if [[ ! -f "$UNIT_SRC" ]]; then
  echo "HATA: servis dosyasi bulunamadi: $UNIT_SRC"
  exit 1
fi

install -m 0644 "$UNIT_SRC" "$UNIT_DST"

echo "[5/6] systemd enable/start..."
systemctl daemon-reload
systemctl enable --now bq25792d.service
systemctl restart bq25792d.service

echo "[6/6] Kontrol..."
echo "---- bqctl status ----"
bqctl status || true
echo
echo "---- bqctl cached --json ----"
bqctl cached --json || true
echo
echo "---- systemctl status bq25792d.service ----"
systemctl status bq25792d.service --no-pager || true

echo
echo "OK"
