# Piper API Server

Este fork agrega un modo servidor local para generar voz por HTTP. El formato de respuesta sigue el estándar simple usado en los proyectos JYV: `success`, `message`, `data` y, en errores, `success: false`, `error`, `message`.

## Inicio rápido

```bash
./piper --server \
  --models models \
  --host 127.0.0.1 \
  --port 8080 \
  --output_dir outputs \
  --cpu-threads 2 \
  --max-text-chunk-bytes 4096 \
  --max-input-bytes 10485760
```

Al iniciar en modo servidor:

1. Lee la carpeta `models/` por defecto.
2. Si la carpeta no existe, la crea.
3. Si no se pasa `--model`, toma el primer modelo `.onnx` que tenga su archivo `.onnx.json` al lado.
4. Si se pasa `--models otra/carpeta`, usa esa carpeta como origen de modelos.
5. Guarda los audios generados en `outputs/` por defecto, o en `--output_dir` si se especifica.

Estructura esperada:

```txt
models/
  es_MX-voice.onnx
  es_MX-voice.onnx.json
outputs/
```

También se puede iniciar con un modelo explícito:

```bash
./piper --server \
  --model models/es_MX-voice.onnx \
  --host 0.0.0.0 \
  --port 8080
```

O usando un nombre relativo dentro de `--models`:

```bash
./piper --server --models models --model es_MX-voice.onnx
```

## Autenticación por token

No hay login ni usuarios. El servidor puede protegerse con un token fijo, útil para integrarlo con tus APIs, workers o paneles.

Prioridad del token:

1. `--api-token TOKEN`
2. Variable de entorno `PIPER_API_TOKEN`
3. Variable de entorno `PIPER_AUTH_TOKEN`
4. Variable de entorno `API_TOKEN`
5. Archivo `.env` en el directorio desde donde arrancas `piper`

Ejemplo `.env`:

```env
PIPER_API_TOKEN=mi-token-largo-y-seguro
```

También se incluye `.env.example`. El archivo real `.env` queda ignorado por Git.

Iniciar usando `.env`:

```bash
./piper --server --models models --host 127.0.0.1 --port 8080
```

Iniciar usando variable de entorno:

```bash
PIPER_API_TOKEN="mi-token-largo-y-seguro" \
./piper --server --models models --host 127.0.0.1 --port 8080
```

Iniciar usando argumento directo:

```bash
./piper --server \
  --models models \
  --api-token "mi-token-largo-y-seguro"
```

Enviar peticiones autenticadas:

```bash
curl http://127.0.0.1:8080/api/v1/status \
  -H "Authorization: Bearer mi-token-largo-y-seguro"
```

También se acepta:

```bash
curl http://127.0.0.1:8080/api/v1/status \
  -H "X-API-Token: mi-token-largo-y-seguro"
```

Si no configuras token, la API queda abierta en el host/puerto configurado y el servidor muestra una advertencia en logs.

### Error `invalid_token`

```json
{
  "success": false,
  "error": "invalid_token",
  "message": "Token inválido, ausente o expirado."
}
```

HTTP status: `401`.

## Cancelación si el cliente cierra conexión

El endpoint `POST /api/v1/tts` detecta de forma cooperativa si el cliente cerró la conexión mientras se genera el audio.

Comportamiento:

1. Antes de iniciar la síntesis revisa si el socket sigue abierto.
2. Durante la síntesis revisa entre chunks, frases y callbacks de escritura.
3. Si detecta desconexión, lanza una cancelación interna.
4. Borra el `.wav` parcial.
5. Libera el mutex de síntesis para que otra petición pueda continuar.

Limitación importante: una llamada individual a ONNX Runtime no se interrumpe a la mitad de forma agresiva. La cancelación ocurre en el siguiente punto seguro. Por eso el chunking inteligente es clave: mantiene cada inferencia pequeña y hace que la cancelación sea rápida sin corromper estado interno.

## Límites de recursos

| Flag | Default | Descripción |
| --- | ---: | --- |
| `--cpu-threads` | ONNX default | Limita hilos usados por cada réplica ONNX Runtime. Recomendado: `2` o `4`. |
| `--max-text-chunk-bytes` | `4096` | Tamaño preferido de chunks inteligentes. No es un corte brusco; intenta preservar frases y preguntas. |
| `--max-input-bytes` | `10485760` | Límite máximo de payload HTTP/directo. |
| `--max-concurrent-jobs` | `2` | Número máximo de síntesis activas al mismo tiempo. |
| `--max-model-replicas` | `2` | Número máximo de réplicas ONNX por modelo para procesar peticiones paralelas del mismo modelo. |

El servidor ya no queda limitado a una sola petición global. Puede procesar varias solicitudes al mismo tiempo hasta `--max-concurrent-jobs`. Para que dos peticiones del mismo modelo corran en paralelo, crea réplicas controladas de ese modelo hasta `--max-model-replicas`. Si se alcanza el límite, responde `server_busy` con HTTP 429 en vez de dejar que CPU/RAM colapsen.

## Endpoints

Base local recomendada:

```txt
http://127.0.0.1:8080
```

Cuando hay token configurado, todos los endpoints reales requieren:

```txt
Authorization: Bearer <token>
```

o:

```txt
X-API-Token: <token>
```

`OPTIONS` queda libre para CORS.

---

## GET `/api/health`

Verifica que el servidor está activo.

### Response 200

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

---

## GET `/api/v1/status`

Devuelve estado general, modelo activo, carpetas, autenticación y límites.

### Response 200

```json
{
  "success": true,
  "message": "Estado obtenido correctamente.",
  "data": {
    "server": "piper",
    "model_loaded": true,
    "active_model": "/ruta/models/es_MX-voice.onnx",
    "models_dir": "/ruta/models",
    "output_dir": "/ruta/outputs",
    "auth": {
      "enabled": true,
      "header": "Authorization: Bearer <token>"
    },
    "limits": {
      "max_input_bytes": 10485760,
      "max_text_chunk_bytes": 4096,
      "max_concurrent_jobs": 2,
      "max_model_replicas": 2,
      "active_jobs": 0
    }
  }
}
```

---

## GET `/api/v1/models`

Lista los modelos encontrados en la carpeta configurada por `--models`.

### Response 200

```json
{
  "success": true,
  "message": "Modelos listados correctamente.",
  "data": {
    "total": 1,
    "models": [
      {
        "name": "es_MX-voice.onnx",
        "model_file": "/ruta/models/es_MX-voice.onnx",
        "config_file": "/ruta/models/es_MX-voice.onnx.json",
        "has_config": true
      }
    ]
  }
}
```

---

## POST `/api/v1/tts`

Genera un archivo WAV desde texto JSON.

### Body

```json
{
  "text": "Hola, ¿cómo estás? Este texto conserva acentos: á, é, í, ó, ú, ñ, ü.",
  "model": "es_MX-Veritasium.onnx",
  "speaker_id": 0,
  "output_file": "saludo.wav"
}
```

Campos:

| Campo | Tipo | Requerido | Descripción |
| --- | --- | --- | --- |
| `text` | string | Sí | Texto a convertir en voz. Debe venir en UTF-8. |
| `model` | string | No | Nombre exacto de un `.onnx` dentro de `--models`. Si se omite, usa el modelo activo cargado al iniciar. |
| `speaker_id` | int | No | ID de speaker para modelos multi-speaker. |
| `output_file` | string | No | Nombre seguro de salida. Solo `.wav`, sin rutas ni `..`. |

### Response 201

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {
    "file": "saludo.wav",
    "model": "es_MX-Veritasium.onnx",
    "model_file": "/ruta/models/es_MX-Veritasium.onnx",
    "path": "/ruta/outputs/saludo.wav",
    "url": "/api/v1/files/saludo.wav",
    "format": "wav",
    "bytes": 245760,
    "audio_seconds": 4.2,
    "infer_seconds": 1.1,
    "real_time_factor": 0.26
  }
}
```

### cURL

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Authorization: Bearer mi-token-largo-y-seguro" \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-Veritasium.onnx","text":"Hola México, ¿cómo estás? ñ á é í ó ú ü","output_file":"demo.wav"}'
```

---

## GET `/api/v1/files/{archivo}`

Descarga un WAV generado dentro de `output_dir`.

### Ejemplo

```bash
curl -L http://127.0.0.1:8080/api/v1/files/demo.wav \
  -H "Authorization: Bearer mi-token-largo-y-seguro" \
  --output demo.wav
```

Seguridad aplicada:

- No acepta `/`.
- No acepta `\\`.
- No acepta `..`.
- Solo sirve archivos ubicados dentro de `output_dir`.

---

# Errores estándar

## `missing_fields`

```json
{
  "success": false,
  "error": "missing_fields",
  "message": "El campo text es obligatorio."
}
```

## `invalid_token`

```json
{
  "success": false,
  "error": "invalid_token",
  "message": "Token inválido, ausente o expirado."
}
```

## `invalid_json`

```json
{
  "success": false,
  "error": "invalid_json",
  "message": "El body debe ser JSON válido."
}
```

## `payload_too_large`

```json
{
  "success": false,
  "error": "payload_too_large",
  "message": "El texto excede el límite permitido."
}
```

## `server_busy`

```json
{
  "success": false,
  "error": "server_busy",
  "message": "El servidor alcanzó el límite de síntesis simultáneas. Intenta nuevamente."
}
```

## `model_not_found`

```json
{
  "success": false,
  "error": "model_not_found",
  "message": "Modelo no encontrado en la carpeta models/."
}
```

## `model_config_missing`

```json
{
  "success": false,
  "error": "model_config_missing",
  "message": "El modelo existe, pero falta su archivo .onnx.json."
}
```

## `synthesis_error`

```json
{
  "success": false,
  "error": "synthesis_error",
  "message": "No se pudo generar el audio."
}
```
