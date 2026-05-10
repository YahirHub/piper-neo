# Piper Neo API Server

Piper Neo agrega un servidor HTTP local para generar TTS con control de CPU, RAM, disco y concurrencia.

```bash
./piper --server --models models
```

Con ese comando el servidor:

1. Crea `models/` si no existe.
2. Crea `outputs/` si no existe.
3. Limpia temporales viejos en `outputs/tmp/`.
4. Escanea modelos `.onnx` y metadata `.onnx.json`.
5. Aplica auto-configuración de recursos.
6. Expone API local con respuestas estándar `success`, `message`, `data`, `error`.

## Autenticación opcional

Si no configuras token, la API queda abierta.

Puedes configurar token con:

```bash
./piper --server --models models --api-token "secret"
```

O en `.env`:

```env
PIPER_API_TOKEN=secret
```

Cuando hay token, usa:

```txt
Authorization: Bearer secret
```

También se acepta:

```txt
X-API-Token: secret
```

## Auto-configuración y overrides

Por defecto solo necesitas:

```bash
./piper --server --models models
```

Puedes ajustar manualmente; por defecto `--cpu-profile auto` detecta CPU/memoria expuesta por Docker/cgroups:

```bash
./piper --server \
  --models models \
  --cpu-profile auto \
  --cpu-threads 2 \
  --max-concurrent-jobs 3 \
  --chunk-workers 3 \
  --max-model-replicas 2 \
  --queue-size 32 \
  --queue-timeout-seconds 60 \
  --max-input-bytes 10485760 \
  --max-text-chunk-bytes 4096 \
  --max-temp-bytes 1073741824 \
  --output-retention-seconds 3600 \
  --models-refresh-seconds 30
```

## Gestión de CPU, RAM y disco

Piper Neo evita el colapso del código original de estas formas:

- Divide textos largos en chunks inteligentes.
- Procesa chunks de varios clientes de forma justa.
- No mantiene todo el audio final en RAM.
- Escribe chunks RAW temporales en disco.
- Ensambla el WAV final al terminar.
- Borra temporales al terminar, fallar o cancelar.
- Limpia `outputs/tmp/` al arrancar.
- Calcula automáticamente el presupuesto temporal si no se especifica `--max-temp-bytes`; `0` desactiva el límite.
- Elimina audios generados después de `--output-retention-seconds`.
- Limita hilos ONNX con `--cpu-threads`.
- Limita trabajos simultáneos con `--max-concurrent-jobs`.
- Limita réplicas ONNX por modelo con `--max-model-replicas`.

Por defecto, los audios generados por API duran 1 hora:

```txt
--output-retention-seconds 3600
```

`0` desactiva la limpieza por retención.

## Logs de conversión

El servidor registra eventos importantes:

```txt
2026-05-09T20:00:00Z Model load started: model=es_MX-Veritasium.onnx
2026-05-09T20:00:01Z Model load finished: model=es_MX-Veritasium.onnx duration_ms=1234
2026-05-09T20:00:05Z TTS job queued: id=tts_xxx model=es_MX-Veritasium.onnx input_bytes=2500 chunks=4
2026-05-09T20:00:08Z TTS job finished: id=tts_xxx model=es_MX-Veritasium.onnx chunks=4 audio_seconds=18.2 infer_seconds=3.7 duration_ms=4100 file=tts_xxx.wav bytes=803924
```

## Formato de respuesta

Éxito:

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {}
}
```

Error:

```json
{
  "success": false,
  "error": "invalid_request",
  "message": "Descripción del error."
}
```

## GET `/api/health`

```bash
curl http://127.0.0.1:8080/api/health
```

```json
{
  "success": true,
  "message": "Servidor Piper activo.",
  "data": {
    "status": "ok",
    "model_loaded": true,
    "time": "2026-05-09T20:00:00Z"
  }
}
```

## GET `/api/v1/status`

Devuelve estado sin exponer rutas absolutas internas.

```json
{
  "success": true,
  "message": "Estado obtenido correctamente.",
  "data": {
    "server": "piper-neo",
    "model_loaded": true,
    "active_model": "es_MX-Veritasium.onnx",
    "models_dir": "models",
    "output_dir": "outputs",
    "auth": {
      "enabled": false,
      "header": "Authorization: Bearer <token>"
    },
    "limits": {
      "max_input_bytes": 10485760,
      "max_text_chunk_bytes": 4096,
      "max_temp_bytes": 1073741824,
      "output_retention_seconds": 3600
    }
  }
}
```

## GET `/api/v1/metrics`

```json
{
  "success": true,
  "data": {
    "jobs": {
      "active": 1,
      "waiting": 0,
      "accepted": 10,
      "completed": 9,
      "cancelled": 1,
      "failed": 0,
      "rejected": 0
    },
    "chunks": {
      "processing": 1,
      "completed": 40,
      "failed": 0
    },
    "storage": {
      "temp_bytes": 0,
      "max_temp_bytes": 1073741824,
      "output_retention_seconds": 3600
    }
  }
}
```

## GET `/api/v1/models`

Lista básica cacheada. No devuelve rutas absolutas, `phoneme_id_map` ni imágenes base64.

```bash
curl http://127.0.0.1:8080/api/v1/models
```

```json
{
  "success": true,
  "message": "Modelos listados correctamente.",
  "data": {
    "total": 1,
    "include": "basic",
    "cached": true,
    "refresh_seconds": 30,
    "models": [
      {
        "file": "es_ARG-Elena.onnx",
        "name": "Español Argentino | Elena",
        "config_file": "es_ARG-Elena.onnx.json",
        "available": true,
        "has_config": true,
        "config_valid": true,
        "language": "es_AR",
        "has_image": true,
        "image_url": "/api/v1/models/es_ARG-Elena.onnx/image"
      }
    ]
  }
}
```

## GET `/api/v1/models?include=metadata`

Incluye metadata útil del `.onnx.json`:

```bash
curl "http://127.0.0.1:8080/api/v1/models?include=metadata"
```

Incluye campos como:

- `dataset`
- `audio.sample_rate`
- `audio.quality`
- `language_info.code`
- `espeak.voice`
- `inference.noise_scale`
- `inference.length_scale`
- `inference.noise_w`
- `num_speakers`
- `piper_version`
- `modelcard` sin `image`

## GET `/api/v1/models?include=technical`

Agrega campos técnicos ligeros:

- `phoneme_type`
- `num_symbols`
- `speaker_id_map`

No devuelve `phoneme_id_map` por defecto para evitar respuestas enormes.

## GET `/api/v1/models/{model}/image`

Sirve la imagen del `modelcard.image` por separado.

```bash
curl http://127.0.0.1:8080/api/v1/models/es_ARG-Elena.onnx/image \
  --output model.jpg
```

Soporta `image/jpeg`, `image/png` y `image/webp` si el JSON viene como `data:image/...;base64,...`.

## POST `/api/v1/tts`

Genera audio desde texto JSON.

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-Veritasium.onnx","text":"Hola México, ¿cómo estás?"}'
```

Body:

```json
{
  "model": "es_MX-Veritasium.onnx",
  "text": "Hola México, ¿cómo estás?",
  "speaker_id": 0
}
```

`output_file` no está soportado en la API. Piper Neo genera nombres seguros automáticamente.

Respuesta:

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {
    "file": "tts_123456789_abcdef.wav",
    "model": "es_MX-Veritasium.onnx",
    "url": "/api/v1/files/tts_123456789_abcdef.wav",
    "format": "wav",
    "chunks": 3,
    "bytes": 245760,
    "audio_seconds": 5.57,
    "infer_seconds": 1.22,
    "real_time_factor": 0.21
  }
}
```

## GET `/api/v1/files/{file}`

Descarga el WAV generado.

```bash
curl http://127.0.0.1:8080/api/v1/files/tts_123456789_abcdef.wav \
  --output salida.wav
```

Los archivos se entregan como `audio/wav` binario. No se devuelve audio en base64 para evitar inflar el peso. El WAV generado usa PCM 16-bit sin metadata extra.

## Errores comunes

| error | status | Descripción |
|---|---:|---|
| `invalid_token` | 401 | Token ausente o incorrecto. |
| `missing_fields` | 400 | Falta `text`. |
| `invalid_json` | 400 | Body JSON inválido. |
| `invalid_request` | 400 | Parámetro inválido o no soportado. |
| `payload_too_large` | 413 | Texto o body excede el límite. |
| `model_not_found` | 404 | El modelo no existe en `models/`. |
| `model_config_missing` | 404 | Falta el `.onnx.json`. |
| `server_busy` | 429 | Cola o trabajos simultáneos saturados. |
| `temp_storage_full` | 507 | Se alcanzó `--max-temp-bytes`. |
| `synthesis_error` | 500 | Error durante la síntesis. |

## Piper Neo `.neo` model packages

The server now accepts both classic Piper voices and Piper Neo packages in the same `models/` directory:

```text
models/
├─ es_MX-Veritasium.onnx
├─ es_MX-Veritasium.onnx.json
└─ es_ARG-Elena.neo
```

A `.neo` package contains the ONNX model, metadata JSON and optional image in one binary file. Internal sections are compressed with zstd, which is lossless and does not reduce audio quality.

### Listing models

```bash
curl http://127.0.0.1:8080/api/v1/models?include=metadata
```

Example item:

```json
{
  "file": "es_ARG-Elena.neo",
  "format": "neo",
  "config_file": "embedded",
  "has_config": true,
  "config_valid": true,
  "has_image": true,
  "image_url": "/api/v1/models/es_ARG-Elena.neo/image",
  "neo": {
    "version": 1,
    "model_compression": "zstd",
    "model_bytes": 65000000,
    "stored_model_bytes": 42000000
  }
}
```

### Using a `.neo` model in TTS

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{"model":"es_ARG-Elena.neo","text":"Hola, ¿cómo estás?"}'
```

### Model image

Images are not embedded in `/models` responses. Use:

```bash
curl http://127.0.0.1:8080/api/v1/models/es_ARG-Elena.neo/image --output voice.jpg
```
