# Nuevo uso de Piper mantenido

## CLI: texto directo

```bash
./piper --model models/es_MX-Veritasium.onnx \
  --text "Hola México, ¿cómo estás?" \
  --output_file hola.wav
```

## CLI: archivo de texto

```bash
./piper --model models/es_MX-Veritasium.onnx \
  --input_file texto.txt \
  --output_file salida.wav
```

El texto se maneja como UTF-8. En Windows se configura la consola en UTF-8 para preservar acentos y símbolos latinos.

## CLI: texto largo

```bash
./piper --model models/es_MX-Veritasium.onnx \
  --input_file texto_largo.txt \
  --output_file salida.wav \
  --max-text-chunk-bytes 4096 \
  --cpu-threads 2
```

## API: inicio mínimo

```bash
./piper --server --models models
```

El servidor crea `models/` y `outputs/` si faltan. Si `models/` contiene al menos un `.onnx` con su `.onnx.json`, carga el primero como modelo inicial.

## API: inicio manual

```bash
./piper --server \
  --models models \
  --model es_MX-Veritasium.onnx \
  --host 127.0.0.1 \
  --port 8080 \
  --cpu-profile balanced \
  --cpu-threads 2 \
  --max-concurrent-jobs 4 \
  --chunk-workers 4 \
  --max-model-replicas 2 \
  --queue-size 32
```

## API: token opcional

Sin token configurado, no pide token.

Con `.env`:

```env
PIPER_API_TOKEN=mi-token-largo-y-seguro
```

Petición autenticada:

```bash
curl http://127.0.0.1:8080/api/v1/status \
  -H "Authorization: Bearer mi-token-largo-y-seguro"
```

## API: generar voz

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{
    "model": "es_MX-Veritasium.onnx",
    "text": "Voy a generar voz con acentos: á é í ó ú ñ ¿Qué tal?",
  }'
```

## API: listar modelos

```bash
curl http://127.0.0.1:8080/api/v1/models
```

Con metadata:

```bash
curl "http://127.0.0.1:8080/api/v1/models?include=metadata"
```

Con datos técnicos pequeños:

```bash
curl "http://127.0.0.1:8080/api/v1/models?include=technical"
```

## API: imagen del modelo

```bash
curl http://127.0.0.1:8080/api/v1/models/es_ARG-Elena.onnx/image \
  --output elena.jpg
```

## API: métricas

```bash
curl http://127.0.0.1:8080/api/v1/metrics
```

## Notas de rendimiento

- `--server --models models` ya aplica auto-configuración.
- No conviene poner `--cpu-threads` igual al total de hilos si tendrás varias solicitudes.
- Para 8 hilos, suele ser mejor `--cpu-threads 2` con `--chunk-workers 3` o `4`.
- Si sube mucho la RAM, baja `--max-model-replicas`.
- Si sube mucho la CPU, baja `--chunk-workers` o usa `--cpu-profile eco`.


## Retención de audios API

En modo API, Piper Neo genera nombres seguros automáticamente y elimina los WAV después de `--output-retention-seconds` segundos. El valor por defecto es `3600` segundos. El parámetro JSON `output_file` no está soportado en `/api/v1/tts`.
