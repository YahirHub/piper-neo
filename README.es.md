# Piper Neo

Piper Neo es la siguiente evolución de Piper TTS: una versión más optimizada, mejorada y preparada para producción, enfocada en textos largos, español latino, uso local/API y control seguro de CPU, RAM y disco.

Este fork mantiene la base de Piper, pero agrega funciones prácticas para servidores, dashboards y sistemas de automatización que necesitan comportamiento estable y predecible.

## Novedades de Piper Neo

- Sistema inteligente de chunks para no romper palabras, preguntas, exclamaciones ni caracteres UTF-8.
- Entrada estable en español latino con acentos, `ñ`, `¿?`, `¡!` y caracteres especiales.
- Entrada directa con `--text`.
- Entrada desde archivo con `--input_file` / `--input-file`.
- Escritura WAV progresiva para no mantener todo el audio final en RAM.
- Servidor HTTP local con `--server`.
- Carpeta de modelos configurable con `--models`.
- Selección de modelo por solicitud usando el campo JSON `model`.
- Token API opcional desde `--api-token`, variables de entorno o `.env`.
- Auto-configuración de recursos al iniciar solo con `--server --models models`.
- Overrides manuales para hilos CPU, trabajos simultáneos, réplicas de modelo, cola y workers de chunks.
- Scheduler justo por chunks para que varios clientes avancen al mismo tiempo y un texto largo no monopolice el motor.
- Control de disco temporal con `--max-temp-bytes`.
- Limpieza automática de temporales al iniciar el servidor.
- Eliminación automática de audios generados por API después de 1 hora por defecto.
- Caché de metadata para `/api/v1/models` con `--models-refresh-seconds`.
- Lectura de metadata desde `.onnx.json` sin devolver `phoneme_id_map` gigante ni imágenes base64 dentro del listado.
- Endpoint separado para imagen del modelo: `/api/v1/models/{model}/image`.
- Respuestas API más seguras, sin exponer rutas absolutas internas del servidor.
- Logs de conversión con timestamp, tiempo de carga del modelo, chunks totales, duración del audio, duración de inferencia, duración total y bytes finales.
- Helper `TTS/bin/resample.py` incluido para flujos de entrenamiento de Piper.

## Uso rápido por CLI

Generar desde texto directo:

```bash
./piper --model models/es_MX-voice.onnx \
  --text "Hola México, ¿cómo estás?" \
  --output_file saludo.wav
```

Generar desde archivo UTF-8:

```bash
./piper --model models/es_MX-voice.onnx \
  --input_file texto.txt \
  --output_file salida.wav \
  --cpu-threads 2
```

## Uso rápido de la API

Iniciar el servidor con ajuste automático de recursos:

```bash
./piper --server --models models
```

Ejemplo recomendado:

```bash
./piper --server \
  --models models \
  --host 127.0.0.1 \
  --port 8080 \
  --cpu-profile auto \
  --output-retention-seconds 3600
```

Generar TTS:

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-voice.onnx","text":"Hola México, ¿cómo estás? ñ á é í ó ú"}'
```

La API genera automáticamente un nombre seguro para el archivo. El parámetro `output_file` ya no está soportado en la API HTTP.

Descargar el audio desde la URL devuelta:

```bash
curl http://127.0.0.1:8080/api/v1/files/tts_xxx.wav --output audio.wav
```

Los WAV generados por API son archivos PCM binarios compactos, sin metadata extra ni envoltura base64. Se eliminan automáticamente después del periodo de retención. Por defecto duran 3600 segundos.

## Endpoints API

- `GET /api/health`
- `GET /api/v1/status`
- `GET /api/v1/metrics`
- `GET /api/v1/models`
- `GET /api/v1/models?include=metadata`
- `GET /api/v1/models?include=technical`
- `GET /api/v1/models/{model}/image`
- `POST /api/v1/tts`
- `GET /api/v1/files/{file}`

Respuesta exitosa estándar:

```json
{
  "success": true,
  "message": "Audio generado exitosamente.",
  "data": {}
}
```

Respuesta de error estándar:

```json
{
  "success": false,
  "error": "invalid_request",
  "message": "Descripción del error."
}
```

## Token API opcional

Si no configuras token, el servidor no lo pide.

Puedes configurar token así:

```bash
./piper --server --models models --api-token "secret"
```

```env
PIPER_API_TOKEN=secret
```

Petición con token:

```bash
curl http://127.0.0.1:8080/api/v1/status \
  -H "Authorization: Bearer secret"
```

## Control de recursos

El modo automático se aplica por defecto en server. Detecta afinidad de CPU, cuota CPU de Docker/cgroups y límite de memoria antes de elegir workers, jobs y réplicas de modelo. Puedes ajustar estos parámetros:

```bash
--cpu-profile auto|eco|balanced|fast|max
--cpu-threads NUM|auto
--max-concurrent-jobs NUM
--chunk-workers NUM
--max-model-replicas NUM
--queue-size NUM
--queue-timeout-seconds NUM
--max-input-bytes NUM
--max-text-chunk-bytes NUM
--max-temp-bytes NUM  # 0 = sin límite; si no se especifica, se calcula automáticamente
--output-retention-seconds NUM
--models-refresh-seconds NUM
```

El servidor evita colapsar dividiendo texto en chunks, escribiendo RAW temporales, ensamblando el WAV final al terminar, ajustando hilos/workers según el hardware disponible, limitando réplicas por memoria y limpiando archivos generados automáticamente.

## Helper para entrenamiento

Se incluye el helper de remuestreo de Coqui TTS en:

```text
TTS/bin/resample.py
```

Ejemplo:

```bash
python TTS/bin/resample.py \
  --input_dir dataset_raw \
  --output_sr 22050 \
  --output_dir dataset_22050 \
  --file_ext wav \
  --n_jobs 8
```

## Documentación

- `README.md`: documentación en inglés.
- `docs/api-server.md`: documentación completa del servidor HTTP.
- `docs/new-piper-usage.md`: uso CLI, archivos de texto y chunks inteligentes.
- `docs/resource-management-plan.md`: notas de gestión de recursos.

## Proyecto original

Development has moved: https://github.com/OHF-Voice/piper1-gpl

## Paquetes de modelo Piper Neo (`.neo`)

Piper Neo también soporta un nuevo formato de voz de un solo archivo: `.neo`.

Un archivo `.neo` contiene:

- el modelo ONNX
- la metadata JSON compatible con Piper
- información de modelcard
- imagen/portada opcional
- metadata de speakers e inferencia

Las secciones internas se comprimen con zstd. Es compresión sin pérdida: los pesos del modelo se conservan, así que no se reduce la calidad de audio. El beneficio es menor peso para distribuir y menos errores porque el modelo y su configuración viajan juntos.

El formato clásico sigue funcionando:

```text
voz.onnx
voz.onnx.json
```

Una voz Piper Neo se puede usar directamente:

```bash
piper --model models/es_MX-Veritasium.neo --text "Hola desde Piper Neo" --output_file hola.wav
```

Exportar una voz clásica ONNX a `.neo`:

```bash
piper \
  --model models/es_MX-Veritasium.onnx \
  --config models/es_MX-Veritasium.onnx.json \
  --export-neo models/es_MX-Veritasium.neo
```

Con imagen opcional:

```bash
piper \
  --model models/es_MX-Veritasium.onnx \
  --config models/es_MX-Veritasium.onnx.json \
  --neo-image portada.jpg \
  --export-neo models/es_MX-Veritasium.neo
```

En modo servidor, `models/` puede contener voces `.onnx` y paquetes `.neo`:

```bash
piper --server --models models
```

`GET /api/v1/models?include=metadata` devuelve ambos formatos sin exponer rutas absolutas internas. Las imágenes no se incrustan en el listado; se consultan por separado:

```text
GET /api/v1/models/{model}/image
```

El contexto técnico recuperable del proyecto está en `neo-docs/`.
