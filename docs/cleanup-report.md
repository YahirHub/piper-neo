# Limpieza de peso del proyecto

## Resumen

Se revisó el ZIP completo usando como fuente de verdad el proyecto entregado y la carpeta `contexto/`.

No se encontraron dependencias instaladas ni salidas de build dentro del paquete:

- Sin `.git/`.
- Sin `node_modules/`.
- Sin `dist/`, `release/`, `out/` ni `.vite/` del cliente Electron.
- Sin `build/` de CMake.
- Sin caches Python comunes como `__pycache__/`, `.pytest_cache/` o `.venv/`.

La mayor parte del peso venía de archivos de prueba/demo y assets pesados, no de instalaciones anteriores.

## Archivos eliminados

| Archivo | Motivo |
| --- | --- |
| `etc/test_voice.onnx` | Modelo ONNX de prueba de aproximadamente 26 MB. No es necesario para compilar ni usar Piper Neo con modelos propios. |
| `etc/test_voice.onnx.json` | Configuración asociada al modelo ONNX de prueba eliminado. |

## Cambios para no romper el build

`CMakeLists.txt` ahora deja las pruebas como opcionales mediante:

```bash
-DPIPER_BUILD_TESTS=ON
```

Por defecto las pruebas quedan desactivadas para evitar depender del modelo pesado de ejemplo. Si se activan pruebas y se quiere ejecutar `ctest`, se puede pasar un modelo ONNX externo:

```bash
cmake -B build -DPIPER_BUILD_TESTS=ON -DPIPER_TEST_VOICE=/ruta/a/voz.onnx
```

Si `PIPER_BUILD_TESTS=ON` pero no existe `PIPER_TEST_VOICE`, se compila `test_piper`, pero no se registra la prueba automática con `ctest`.

## Assets optimizados

Se recomprimieron y redujeron los dos banners PNG manteniendo compatibilidad con GitHub y Electron:

- `assets/branding/piper-neo-banner.png`
- `apps/electron-client/src/renderer/assets/piper-neo-banner.png`

## Archivos grandes conservados

Se conservaron los audios de `notebooks/wav/` porque están referenciados por notebooks de entrenamiento/inferencia y no parecen ser archivos temporales de instalación.

También se conservó `package-lock.json` porque es importante para builds reproducibles del cliente Electron.

## Reglas agregadas a `.gitignore`

Se agregaron exclusiones para evitar que vuelvan a entrar al repo:

- `outputs/`
- `models/*` manteniendo `models/.gitkeep`
- `etc/test_voice.onnx`
- `etc/test_voice.onnx.json`
