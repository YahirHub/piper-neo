# Piper Neo — contexto técnico recuperable

Este directorio existe para guardar el contexto del proyecto dentro del propio repositorio. Si una conversación pierde contexto, carga el proyecto y pide leer `neo-docs/` para recuperar el estado, decisiones técnicas y próximos pasos.

## Estado actual

Piper Neo es una evolución optimizada de Piper TTS. Mantiene compatibilidad con el formato clásico:

- `voz.onnx`
- `voz.onnx.json`

También agrega el formato propio:

- `voz.neo`

El formato `.neo` es un contenedor binario propio que incluye metadata JSON, modelo ONNX interno e imagen opcional. Las secciones internas se guardan comprimidas con zstd para reducir peso sin perder calidad de audio, porque los pesos del modelo se conservan exactamente.

## Componentes principales

- `src/cpp/piper.cpp` / `piper.hpp`: inferencia, fonemización, chunking inteligente y escritura WAV progresiva.
- `src/cpp/server.cpp` / `server.hpp`: HTTP API, autenticación opcional, scheduler justo por chunks, cache de modelos y endpoints.
- `src/cpp/neo_model.cpp` / `neo_model.hpp`: lectura, inspección, exportación y extracción del formato `.neo`.
- `docs/`: documentación de usuario/API.
- `neo-docs/`: contexto técnico del proyecto y decisiones de arquitectura.

## Comandos clave

Exportar un paquete `.neo` desde el formato clásico:

```bash
piper --model models/es_MX-Veritasium.onnx \
  --config models/es_MX-Veritasium.onnx.json \
  --export-neo models/es_MX-Veritasium.neo
```

Usar `.neo` directamente:

```bash
piper --model models/es_MX-Veritasium.neo --text "Hola" --output_file hola.wav
```

Servidor con carpeta de modelos:

```bash
piper --server --models models
```

En `/api/v1/models`, Piper Neo lista `.onnx` y `.neo`.

## Decisiones importantes

1. `.neo` no reemplaza ONNX Runtime. El modelo ONNX se mantiene dentro del contenedor.
2. Al cargar `.neo`, se extrae a un caché temporal administrado y luego se carga con ONNX Runtime.
3. La compresión zstd es sin pérdida: reduce espacio en disco/distribución, pero no altera pesos ni calidad.
4. `/api/v1/models` no devuelve imágenes base64 para no inflar respuestas.
5. Las imágenes se sirven por `GET /api/v1/models/{model}/image`.
6. El server no exige token si no se define `PIPER_API_TOKEN`, `.env` o `--api-token`.
7. El output API ya no acepta `output_file`; Piper Neo genera nombres seguros y borra audios después de 1 hora por defecto.

