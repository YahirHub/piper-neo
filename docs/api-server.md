# Piper API Server

Este fork agrega un modo servidor local para generar voz por HTTP. El formato de respuesta sigue el estándar usado en proyectos JYV: respuestas con `success`, `message`, `data` y errores con `success: false`, `error`, `message`.

## Inicio rápido recomendado

```bash
./piper --server --models models
```

Con ese comando el servidor:

1. Crea `models/` si no existe.
2. Busca modelos `.onnx` dentro de `models/`.
3. Usa el primer modelo que tenga su `.onnx.json` como modelo inicial.
4. Crea `outputs/` para los WAV generados.
5. Aplica auto-configuración de CPU, concurrencia, workers, réplicas y cola.
6. No pide token si no existe `PIPER_API_TOKEN`, `PIPER_AUTH_TOKEN`, `API_TOKEN`, `.env` o `--api-token`.

Estructura esperada:

```txt
models/
  es_MX-Veritasium.onnx
  es_MX-Veritasium.onnx.json
outputs/
```

También puedes iniciar con parámetros explícitos:

```bash
./piper --server \
  --models models \
  --model es_MX-Veritasium.onnx \
  --host 0.0.0.0 \
  --port 8080 \
  --output_dir outputs \
  --cpu-profile balanced \
  --cpu-threads 2 \
  --max-concurrent-jobs 4 \
  --chunk-workers 4 \
  --max-model-replicas 2 \
  --queue-size 32 \
  --queue-timeout-seconds 60
```

## Auto-configuración de recursos

Por defecto no necesitas calcular hilos ni workers. Al iniciar con `--server --models models`, Piper detecta `std::thread::hardware_concurrency()` y aplica un perfil `balanced`.

Perfiles disponibles:

| Perfil | Uso recomendado |
| --- | --- |
| `eco` | VPS pequeño, menos CPU, más estable. |
| `balanced` | Default recomendado. |
| `fast` | Más velocidad con consumo moderado/alto. |
| `max` | Uso local o máquina dedicada. |

Flags manuales:

| Flag | Default | Descripción |
| --- | --- | --- |
| `--cpu-profile` | `balanced` | Perfil automático: `eco`, `balanced`, `fast`, `max`. |
| `--cpu-threads` | `auto` en server | Hilos ONNX por worker/réplica. Acepta número o `auto`. |
| `--max-concurrent-jobs` | `auto` | Máximo de trabajos activos aceptados por el scheduler. |
| `--chunk-workers` | `auto` | Hilos del pool que procesan chunks de forma justa. |
| `--max-model-replicas` | `auto` | Réplicas ONNX máximas por modelo. |
| `--queue-size` | `auto` | Máximo de trabajos activos/en espera antes de responder `server_busy`. |
| `--queue-timeout-seconds` | `60` | Tiempo máximo esperando antes de cancelar una solicitud en cola. |
| `--max-input-bytes` | `10485760` | Máximo del body o texto recibido. |
| `--max-text-chunk-bytes` | `4096` | Tamaño preferido de chunks inteligentes. |

La idea no es usar todos los hilos por cada request. Si una máquina tiene 8 hilos y llegan 5 solicitudes, el scheduler reparte chunks entre clientes y ONNX usa pocos hilos por worker para evitar oversubscription.

## Scheduler justo por chunks

Cada solicitud TTS se divide en chunks inteligentes. El scheduler usa round-robin para que un texto enorme no bloquee a textos pequeños.

Ejemplo interno:

```txt
Cliente A: A1 A2 A3 A4
Cliente B: B1 B2
Cliente C: C1 C2 C3

Orden aproximado:
A1 → B1 → C1 → A2 → B2 → C2 → A3 → C3 → A4
```

Cada chunk se sintetiza en un archivo temporal RAW dentro de `outputs/tmp/<job_id>/`. Al finalizar, el servidor arma un WAV final en orden y borra temporales. Esto evita guardar todo el audio en RAM y permite concurrencia real controlada.

## Autenticación por token

No hay usuarios ni login. Si configuras token, todos los endpoints reales piden token. Si no configuras token, la API queda abierta.

Prioridad:

1. `--api-token TOKEN`
2. `PIPER_API_TOKEN`
3. `PIPER_AUTH_TOKEN`
4. `API_TOKEN`
5. `.env` en el directorio actual

`.env`:

```env
PIPER_API_TOKEN=mi-token-largo-y-seguro
```

Headers aceptados:

```txt
Authorization: Bearer mi-token-largo-y-seguro
```

```txt
X-API-Token: mi-token-largo-y-seguro
```

Error:

```json
{
  "success": false,
  "error": "invalid_token",
  "message": "Token inválido, ausente o expirado."
}
```

## Cancelación si el cliente se desconecta

`POST /api/v1/tts` revisa el socket del cliente mientras espera y mientras procesa chunks. Si el cliente cierra conexión:

1. Marca el job como cancelado.
2. No programa más chunks.
3. Espera a que el chunk que ya entró a ONNX termine en un punto seguro.
4. Borra temporales y WAV parcial.
5. Libera réplicas y espacio de cola.

Una inferencia ONNX individual no se mata a mitad de llamada para no dejar estado interno inconsistente. La cancelación ocurre en el siguiente punto seguro.

# Endpoints

Base local:

```txt
http://127.0.0.1:8080
```

## GET `/api/health`

```json
{
  "success": true,
  "message": "Servidor Piper activo.",
  "data": {
    "status": "ok",
    "model_loaded": true,
    "time": "2026-05-09T12:00:00Z"
  }
}
```

## GET `/api/v1/status`

Devuelve estado, carpetas, auth y política real de recursos.

```json
{
  "success": true,
  "message": "Estado obtenido correctamente.",
  "data": {
    "server": "piper",
    "model_loaded": true,
    "active_model": "/app/models/es_MX-Veritasium.onnx",
    "models_dir": "/app/models",
    "output_dir": "/app/outputs",
    "auth": {
      "enabled": false,
      "header": "Authorization: Bearer <token>"
    },
    "limits": {
      "max_input_bytes": 10485760,
      "max_text_chunk_bytes": 4096
    },
    "resource_policy": {
      "mode": "auto",
      "profile": "balanced",
      "hardware_threads": 8,
      "cpu_threads_per_worker": 2,
      "max_concurrent_jobs": 3,
      "chunk_workers": 3,
      "max_model_replicas": 2,
      "queue_size": 32,
      "queue_timeout_seconds": 60,
      "active_jobs": 0,
      "waiting_jobs": 0
    }
  }
}
```

## GET `/api/v1/metrics`

```json
{
  "success": true,
  "message": "Métricas obtenidas correctamente.",
  "data": {
    "jobs": {
      "active": 2,
      "waiting": 1,
      "accepted": 120,
      "completed": 118,
      "cancelled": 1,
      "failed": 0,
      "rejected": 1
    },
    "chunks": {
      "processing": 3,
      "completed": 780,
      "failed": 0
    },
    "resources": {}
  }
}
```

## GET `/api/v1/models`

Lista modelos. Por defecto usa `include=basic`.

```bash
curl http://127.0.0.1:8080/api/v1/models
```

Respuesta básica:

```json
{
  "success": true,
  "message": "Modelos listados correctamente.",
  "data": {
    "total": 1,
    "include": "basic",
    "models": [
      {
        "file": "es_ARG-Elena.onnx",
        "name": "Español Argentino | Elena",
        "model_file": "models/es_ARG-Elena.onnx",
        "config_file": "models/es_ARG-Elena.onnx.json",
        "available": true,
        "has_config": true,
        "config_valid": true,
        "language": "es_AR",
        "has_image": true,
        "image_url": "/api/v1/models/es_ARG-Elena.onnx/image",
        "modelcard": {
          "id": "es_ARG-Elena",
          "name": "Español Argentino | Elena",
          "description": "Modelo creado por el equipo de HirLab.",
          "language": "es_AR",
          "voiceprompt": "Voz femenina joven y energética, ideal para contenido juvenil, redes sociales y aplicaciones modernas",
          "sha256": "478F6C..."
        }
      }
    ]
  }
}
```

## GET `/api/v1/models?include=metadata`

Incluye metadata útil del `.onnx.json`, pero nunca devuelve `modelcard.image` ni `phoneme_id_map` porque pueden ser enormes.

Campos leídos si existen:

- `dataset`
- `audio.sample_rate`
- `audio.quality`
- `espeak.voice`
- `language.code`
- `inference.noise_scale`
- `inference.length_scale`
- `inference.noise_w`
- `num_speakers`
- `piper_version`
- `modelcard.id`
- `modelcard.name`
- `modelcard.description`
- `modelcard.language`
- `modelcard.voiceprompt`
- `modelcard.sha256`
- `modelcard.image` solo como `has_image` e `image_url`

El `.onnx.json` de ejemplo incluye precisamente campos como `audio`, `espeak`, `language`, `inference`, `num_speakers`, `piper_version` y `modelcard`, por eso se exponen de forma ligera en `/models`.

## GET `/api/v1/models?include=technical`

Agrega datos técnicos pequeños:

- `phoneme_type`
- `num_symbols`
- `speaker_id_map`

No devuelve `phoneme_id_map` por defecto.

## GET `/api/v1/models/{model}/image`

Sirve la imagen del `modelcard.image` si existe.

```bash
curl http://127.0.0.1:8080/api/v1/models/es_ARG-Elena.onnx/image \
  --output elena.jpg
```

Soporta imágenes embebidas como:

```txt
data:image/jpeg;base64,...
data:image/png;base64,...
data:image/webp;base64,...
```

Errores:

```json
{
  "success": false,
  "error": "not_found",
  "message": "El modelo no tiene imagen."
}
```

```json
{
  "success": false,
  "error": "invalid_image",
  "message": "La imagen del modelo no tiene un formato válido."
}
```

## POST `/api/v1/tts`

Genera un WAV.

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{
    "model": "es_MX-Veritasium.onnx",
    "text": "Hola México, ¿cómo estás? ñ á é í ó ú",
    "output_file": "demo.wav"
  }'
```

Body:

| Campo | Tipo | Requerido | Descripción |
| --- | --- | --- | --- |
| `text` | string | sí | Texto UTF-8. |
| `model` | string | no | Nombre seguro de un `.onnx` dentro de `--models`. |
| `speaker_id` | int | no | Speaker para modelos multi-speaker. |
| `output_file` | string | no | Nombre seguro `.wav`. |

Respuesta:

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {
    "file": "demo.wav",
    "model": "es_MX-Veritasium.onnx",
    "model_file": "/app/models/es_MX-Veritasium.onnx",
    "path": "/app/outputs/demo.wav",
    "url": "/api/v1/files/demo.wav",
    "format": "wav",
    "chunks": 4,
    "bytes": 530420,
    "audio_seconds": 12.4,
    "infer_seconds": 3.1,
    "real_time_factor": 0.25
  }
}
```

## GET `/api/v1/files/{archivo}`

Descarga un WAV generado.

```bash
curl http://127.0.0.1:8080/api/v1/files/demo.wav --output demo.wav
```

Seguridad:

- No permite `/`.
- No permite `\\`.
- No permite `..`.
- Solo sirve desde `output_dir`.

# Errores comunes

| Error | HTTP | Significado |
| --- | ---: | --- |
| `invalid_token` | 401 | Token faltante o incorrecto. |
| `missing_fields` | 400 | Falta `text`. |
| `invalid_json` | 400 | Body JSON inválido. |
| `payload_too_large` | 413 | Texto/payload excede límite. |
| `invalid_request` | 400 | Modelo, archivo o parámetro inválido. |
| `model_not_found` | 404 | Modelo no existe en `--models`. |
| `model_config_missing` | 404 | Falta `.onnx.json`. |
| `server_busy` | 429 | Cola llena o timeout de cola. |
| `synthesis_error` | 500 | Fallo durante síntesis. |
