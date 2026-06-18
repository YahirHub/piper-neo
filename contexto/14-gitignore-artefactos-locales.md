# 14. Gitignore para artefactos locales de build

## Objetivo

Evitar que se suban al repositorio archivos generados por compilaciones locales, paquetes de distribución, cachés de CMake/Ninja/Python y binarios producidos en Windows.

## Cambios agregados

Se ampliaron reglas en `.gitignore` para ignorar:

- `build-winlibs/` y `dist-winlibs/`, usados por el build local con WinLibs/MinGW.
- `build-msvc/`, `dist-msvc/`, `build-windows/` y `dist-windows/`, para builds alternativos de Windows.
- `build-winlibs-patch.cmake`, generado por scripts locales de compatibilidad.
- Cachés de CMake, Ninja, Make y pruebas locales.
- Binarios nativos de compilación como `.exe`, `.dll`, `.lib`, `.pdb`, `.obj` y similares.
- Cachés de Python y herramientas de análisis.
- Respaldos/parches locales como `.orig`, `.rej`, `.neo-backup` y `.patch.local`.

## Criterio de repositorio limpio

Los scripts fuente sí deben quedarse versionados, por ejemplo:

- `script/build-windows.cmd`
- `script/build-windows.py`
- archivos de documentación
- archivos de configuración ejemplo

Los resultados de ejecutar esos scripts no deben versionarse.

## Nota para continuar

El último commit confirmado por el usuario antes de este ajuste fue:

```txt
Corregir flujo de normalización y edición de modelos
```

Este cambio debe aplicarse encima de ese estado local o encima del estado más reciente del repositorio, sin incluir carpetas generadas por builds previos.
