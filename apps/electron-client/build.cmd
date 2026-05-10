@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo.
echo ============================================================
echo   Piper Neo Client - build Windows
echo ============================================================
echo.

where node >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Node.js no esta instalado o no esta en PATH.
  echo Instala Node.js 20+ y vuelve a ejecutar este script.
  exit /b 1
)

where npm >nul 2>nul
if errorlevel 1 (
  echo [ERROR] npm no esta instalado o no esta en PATH.
  exit /b 1
)

for /f "tokens=*" %%v in ('node -v') do set NODE_VERSION=%%v
echo [INFO] Node: !NODE_VERSION!

if not exist node_modules (
  echo [INFO] Instalando dependencias...
  if exist package-lock.json (
    call npm ci
  ) else (
    call npm install
  )
  if errorlevel 1 (
    echo [ERROR] Fallo la instalacion de dependencias.
    exit /b 1
  )
)

echo [INFO] Limpiando build anterior...
if exist dist rmdir /s /q dist
if exist release rmdir /s /q release

echo [INFO] Compilando TypeScript/Vite...
call npm run build
if errorlevel 1 (
  echo [ERROR] Fallo npm run build.
  exit /b 1
)

echo [INFO] Generando paquete Windows con electron-builder...
call npx electron-builder --win --x64
if errorlevel 1 (
  echo [ERROR] Fallo electron-builder para Windows.
  exit /b 1
)

echo.
echo [OK] Build terminado. Revisa la carpeta:
echo %CD%\release
echo.
endlocal
