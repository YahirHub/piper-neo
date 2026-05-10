#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

printf '\n============================================================\n'
printf '  Piper Neo Client - build Linux\n'
printf '============================================================\n\n'

if ! command -v node >/dev/null 2>&1; then
  echo "[ERROR] Node.js no está instalado o no está en PATH. Instala Node.js 20+." >&2
  exit 1
fi

if ! command -v npm >/dev/null 2>&1; then
  echo "[ERROR] npm no está instalado o no está en PATH." >&2
  exit 1
fi

echo "[INFO] Node: $(node -v)"

if [ ! -d node_modules ]; then
  echo "[INFO] Instalando dependencias..."
  if [ -f package-lock.json ]; then
    npm ci
  else
    npm install
  fi
fi

echo "[INFO] Limpiando build anterior..."
rm -rf dist release

echo "[INFO] Compilando TypeScript/Vite..."
npm run build

echo "[INFO] Detectando arquitectura..."
arch="$(uname -m)"
case "$arch" in
  x86_64|amd64) electron_arch="x64" ;;
  aarch64|arm64) electron_arch="arm64" ;;
  armv7l|armv7*) electron_arch="armv7l" ;;
  *)
    echo "[WARN] Arquitectura no reconocida: $arch. Electron Builder intentará usar configuración por defecto."
    electron_arch=""
    ;;
esac

if [ -n "$electron_arch" ]; then
  echo "[INFO] Generando paquete Linux para $electron_arch..."
  npx electron-builder --linux --"$electron_arch"
else
  echo "[INFO] Generando paquete Linux..."
  npx electron-builder --linux
fi

printf '\n[OK] Build terminado. Revisa la carpeta:\n%s/release\n\n' "$PWD"
