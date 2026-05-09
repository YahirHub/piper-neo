# Piper API Server

Este fork agrega un modo servidor local para generar voz por HTTP sin agregar login. El formato de respuesta sigue el estándar simple usado en los proyectos JYV: `success`, `message`, `data` y, en errores, `success: false`, `error`, `message`.

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

## Límites de recursos

| Flag | Default | Descripción |
| --- | ---: | --- |
| `--cpu-threads` | ONNX default | Limita hilos usados por ONNX Runtime. Recomendado: `2` o `4`. |
| `--max-text-chunk-bytes` | `4096` | Tamaño preferido de chunks inteligentes. No es un corte brusco; intenta preservar frases y preguntas. |
| `--max-input-bytes` | `10485760` | Límite máximo de payload HTTP/directo. |

El servidor procesa una síntesis a la vez. Si llega otra petición mientras ya está generando audio, responde `server_busy` con HTTP 429. Esto evita que varias peticiones largas tumben CPU/RAM.

## Endpoints

Base local recomendada:

```txt
http://127.0.0.1:8080
```

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

Devuelve estado general, modelo activo, carpetas y límites.

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
    "limits": {
      "max_input_bytes": 10485760,
      "max_text_chunk_bytes": 4096
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
  "speaker_id": 0,
  "output_file": "saludo.wav"
}
```

Campos:

| Campo | Tipo | Requerido | Descripción |
| --- | --- | --- | --- |
| `text` | string | Sí | Texto a convertir en voz. Debe venir en UTF-8. |
| `speaker_id` | int | No | ID de speaker para modelos multi-speaker. |
| `output_file` | string | No | Nombre seguro de salida. Solo `.wav`, sin rutas ni `..`. |

### Response 201

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {
    "file": "saludo.wav",
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
  -H "Content-Type: application/json" \
  -d '{"text":"Hola México, ¿cómo estás? ñ á é í ó ú ü","output_file":"demo.wav"}'
```

---

## GET `/api/v1/files/{archivo}`

Descarga un WAV generado dentro de `output_dir`.

### Ejemplo

```bash
curl -L http://127.0.0.1:8080/api/v1/files/demo.wav --output demo.wav
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
  "message": "El servidor está procesando otra síntesis. Intenta nuevamente."
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

## `not_found`

```json
{
  "success": false,
  "error": "not_found",
  "message": "Endpoint no encontrado."
}
```

# Notas de producción

- No incluye autenticación por diseño.
- Para exponerlo fuera del servidor local, colócalo detrás de Caddy, Traefik, Nginx o Cloudflare Tunnel.
- Usa `--host 127.0.0.1` si solo quieres acceso local.
- Usa `--host 0.0.0.0` solo si sabes que el puerto estará protegido por firewall/proxy.
- Para evitar saturación, usa `--cpu-threads 2` y deja el servidor con concurrencia de síntesis 1.
