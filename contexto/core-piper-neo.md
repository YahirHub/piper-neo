# Cambios core de Piper Neo ya trabajados

## Fix Windows binario silencioso

Commit anterior propuesto/real:

```txt
d9daf60 fix: corregir binario windows silencioso
```

Cambios:

- `main.cpp` envuelto en `try/catch` global.
- Muestra errores legibles:
  - `piper: error: ...`
  - `Run with --help to see available options.`
- `--help` imprime correctamente en consola Windows.
- Alias `--serve` para `--server`.
- En modo server, si se pasa una carpeta por error en `--model`, se trata como `--models`.
- Workflow Windows copia DLLs runtime de vcpkg.
- Workflow hace smoke test con `piper.exe --help`.
- CMake usa runtime MSVC estático para reducir DLLs faltantes.

## Formato `.neo`

Commit previo:

```txt
7c23834 feat: agregar formato neo con zstd
```

Características:

- Archivos principales:
  - `src/cpp/neo_model.hpp`
  - `src/cpp/neo_model.cpp`
- `.neo` es contenedor binario propio.
- Header magic: `PIPERNEO`.
- Versión 1.
- Secciones:
  - `metadata.json`
  - `model.onnx`
  - `image` opcional.
- Compresión zstd si disponible.
- Export desde `.onnx` + `.onnx.json`.
- Imagen desde `--neo-image` o desde `modelcard.image` base64.
- Server sirve imagen desde `.neo` en `/api/v1/models/{model}/image`.
- ModelCache carga `.neo` extrayendo a cache temporal.

Flags CLI:

```txt
--export-neo FILE
--neo-image FILE
--neo-compression-level NUM
```

`.neo` puede usarse como `--model` sin `--config`.

## Server resource management

Commit previo:

```txt
7aca2f8 feat: optimizar retencion y metadata api
```

Incluye:

- `maxTempBytes`.
- `outputRetentionSeconds`.
- `modelsRefreshSeconds`.
- Cache de `scanModels`.
- Limpieza de `outputs/tmp`.
- Limpieza de audios expirados.
- Scheduler justo con allocated temp bytes.
- Error `temp_storage_full` → HTTP 507.
- Cancelación mejorada.
- API no acepta `output_file`.
- Respuesta TTS sin rutas internas.
- `/status` sin rutas absolutas.
- Logs de carga de modelo y TTS queued/finished.

Flags:

```txt
--max-temp-bytes
--output-retention-seconds
--models-refresh-seconds
```

## Auto hardware

Artefacto:

```txt
/mnt/data/piper-neo-auto-hardware.zip
```

Cambios:

- Perfil de recursos por defecto: `auto`.
- Detección de CPU disponible considerando afinidad y cuotas Docker/cgroups.
- Detección de memoria disponible considerando límites del sistema/cgroups.
- Cálculo automático de hilos ONNX, workers, jobs concurrentes, réplicas y cola.
- Ajuste de presupuesto temporal según memoria detectada.
- Exposición de memoria detectada y `max_temp_bytes` en política de recursos de API.
- Documentación actualizada.
