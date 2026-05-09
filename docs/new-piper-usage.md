# Nuevo uso de Piper CLI

Este fork agrega entrada directa por texto, entrada por archivo, chunking inteligente para textos largos y modo servidor.

## Generar audio desde texto directo

```bash
./piper \
  --model models/es_MX-voice.onnx \
  --text "Hola México, ¿cómo estás? Este texto conserva ñ, acentos y signos." \
  --output_file saludo.wav
```

## Generar audio desde archivo `.txt`

```bash
./piper \
  --model models/es_MX-voice.onnx \
  --input_file texto.txt \
  --output_file salida.wav
```

El archivo se lee en modo binario y se procesa como UTF-8. Esto evita conversiones raras del shell y ayuda a conservar español latino:

```txt
á é í ó ú
Á É Í Ó Ú
ñ Ñ
ü Ü
¿ ?
¡ !
“comillas”
— guion largo
```

Si el archivo trae BOM UTF-8, se elimina automáticamente en la primera línea.

## Generar audio desde stdin

```bash
cat texto.txt | ./piper \
  --model models/es_MX-voice.onnx \
  --output_file salida.wav
```

## Chunking inteligente

Antes el texto largo podía cortarse de forma muy simple por bytes. Ahora el divisor intenta respetar este orden:

1. Preguntas/exclamaciones españolas completas: `¿...?`, `¡...!`.
2. Párrafos.
3. Frases terminadas en `.`, `?`, `!` o `…`.
4. Pausas suaves: `,`, `;`, `:`.
5. Espacios en blanco.
6. Como último recurso, corte seguro en frontera UTF-8.

Esto evita cortes como:

```txt
¿Puedes explicarme cómo funcio
na este sistema?
```

Y prefiere mantenerlo como:

```txt
¿Puedes explicarme cómo funciona este sistema?
```

## Límite preferido de chunk

```bash
./piper \
  --model models/es_MX-voice.onnx \
  --input_file libro.txt \
  --output_file libro.wav \
  --max-text-chunk-bytes 4096
```

`--max-text-chunk-bytes` es un límite preferido, no un corte ciego. El sistema puede extenderse un poco para cerrar una pregunta o una palabra, pero si una frase es demasiado grande, corta por espacio para proteger RAM/CPU.

## Limitar CPU

```bash
./piper \
  --model models/es_MX-voice.onnx \
  --input_file texto_largo.txt \
  --output_file salida.wav \
  --cpu-threads 2
```

## Modo servidor

```bash
./piper --server --models models --host 127.0.0.1 --port 8080 --max-concurrent-jobs 2 --max-model-replicas 2
```

Ver documentación completa:

```txt
docs/api-server.md
```


## Modo servidor con token

Puedes proteger el servidor sin login usando un token fijo en `.env`, variable de entorno o argumento.

`.env`:

```env
PIPER_API_TOKEN=mi-token-largo-y-seguro
```

Servidor:

```bash
./piper --server --models models --host 127.0.0.1 --port 8080
```

Petición:

```bash
curl http://127.0.0.1:8080/api/v1/status \
  -H "Authorization: Bearer mi-token-largo-y-seguro"
```

También puedes pasarlo directo:

```bash
./piper --server --models models --api-token "mi-token-largo-y-seguro"
```

## Seleccionar modelo por petición API

Si tienes varios modelos dentro de `models/`, puedes indicar cuál usar en cada petición:

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-Veritasium.onnx","text":"Hola, ¿cómo estás?","output_file":"veritasium.wav"}'
```

El campo `model` debe ser solo el nombre del archivo `.onnx`, sin rutas. El servidor usa exactamente ese modelo y su archivo `.onnx.json` correspondiente.

## Flags nuevos

| Flag | Descripción |
| --- | --- |
| `--text` | Texto directo a sintetizar. |
| `--input_file`, `--input-file` | Archivo de texto a sintetizar. |
| `--server` | Inicia servidor HTTP local. |
| `--host` | IP de escucha del servidor. Default: `127.0.0.1`. |
| `--port` | Puerto del servidor. Default: `8080`. |
| `--models` | Carpeta de modelos. Default: `models/`. |
| `--api-token` | Token fijo para proteger el servidor API. También puede venir de `PIPER_API_TOKEN` o `.env`. Si no se define, no se pide token. |
| `--max-concurrent-jobs` | Máximo de TTS simultáneos en API. Default: `2`. |
| `--max-model-replicas` | Máximo de réplicas ONNX por modelo para paralelismo del mismo modelo. Default: `2`. |
| `--max-input-bytes` | Límite de entrada directa/API. Default: `10485760`. |
| `--max-text-chunk-bytes` | Tamaño preferido de chunks inteligentes. Default: `4096`. |

## Recomendación para textos enormes

```bash
./piper \
  --model models/es_MX-voice.onnx \
  --input_file texto_muy_largo.txt \
  --output_file texto_muy_largo.wav \
  --cpu-threads 2 \
  --max-text-chunk-bytes 4096
```

Esto procesa el texto por partes y escribe el WAV de forma progresiva, evitando mantener todo el audio en RAM.
