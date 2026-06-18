# 12. Build local de Windows con WinLibs/MinGW

## Objetivo

Permitir generar una carpeta distribuible de Piper Neo en Windows usando WinLibs/MinGW, sin instalar Visual Studio ni Visual C++ Build Tools.

## Cambios aplicados

- Se agregó `script/build-windows.cmd` como flujo local de build usando solo CMD + CMake.
- El script genera una carpeta limpia en `dist-winlibs/piper-neo-windows`.
- El script no empaqueta ejecutables internos como `example.exe`, `test_*.exe`, `piper_phonemize_exe.exe` o `espeak-ng.exe`.
- El script valida que existan los archivos esenciales:
  - `piper.exe`
  - `onnxruntime.dll`
  - `libpiper_phonemize.dll`
  - `libespeak-ng.dll`
  - `libtashkeel_model.ort`
  - `espeak-ng-data/phontab`
- La copia de `espeak-ng-data` busca específicamente la carpeta que contiene `phontab`, evitando copiar carpetas incompletas.
- Se limpian variables TLS para evitar que CMake genere rutas Windows mal escapadas como `C:\Program Files\...` dentro de scripts `.cmake`.
- Se genera un parche temporal `build-winlibs-patch.cmake` para corregir archivos generados por CMake y compatibilizar `piper-phonemize` con WinLibs/MinGW.

## Corrección en código C++

La ruta del ejecutable ahora se detecta con `_WIN32`, no con `_MSC_VER`.

Motivo:

- MSVC define `_MSC_VER` y `_WIN32`.
- MinGW/WinLibs define `_WIN32`, pero no `_MSC_VER`.
- Si se usa `_MSC_VER` para detectar Windows, el binario MinGW cae en la ruta Linux e intenta resolver `/proc/self/exe`, lo cual falla en Windows.

## Uso

Desde la raíz del proyecto:

```cmd
script\build-windows.cmd clean
```

Para recompilar sin borrar todo:

```cmd
script\build-windows.cmd
```

## Salida

La carpeta final queda en:

```txt
dist-winlibs/piper-neo-windows
```

Esta carpeta puede copiarse a otra ubicación y ejecutar:

```cmd
piper.exe --server
```
