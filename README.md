# Piper mantenido

Fork de mantenimiento de Piper enfocado en uso estable con textos largos, español latino y API local.

## Cambios principales de este mantenimiento

- Entrada directa con `--text`.
- Entrada desde archivo con `--input_file` / `--input-file`.
- Preservación de UTF-8 para acentos, `ñ`, signos `¿?`, `¡!` y caracteres especiales.
- Chunking inteligente para no romper palabras, preguntas ni frases cuando sea posible.
- Escritura WAV progresiva para evitar cargar todo el audio en RAM.
- Límite de CPU con `--cpu-threads`.
- Modo API local con `--server`.
- Carpeta de modelos configurable con `--models`.
- Selección de modelo por petición con `model`.
- Token opcional por `.env`/variable de entorno.
- Solicitudes paralelas controladas con `--max-concurrent-jobs` y `--max-model-replicas`.

## CLI rápido

```bash
./piper --model models/es_MX-voice.onnx --text "Hola México, ¿cómo estás?" --output_file saludo.wav
```

```bash
./piper --model models/es_MX-voice.onnx --input_file texto.txt --output_file salida.wav --cpu-threads 2
```

## API rápido

```bash
./piper --server --models models --host 127.0.0.1 --port 8080 --cpu-threads 2 --max-concurrent-jobs 2
```

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-voice.onnx","text":"Hola México, ¿cómo estás? ñ á é í ó ú","output_file":"demo.wav"}'
```

## Documentación

- `docs/new-piper-usage.md`: uso nuevo de CLI, archivo de texto y chunking inteligente.
- `docs/api-server.md`: documentación completa del servidor HTTP.
- `docs/resource-management-plan.md`: notas de gestión de recursos.

## Proyecto original

Development has moved: https://github.com/OHF-Voice/piper1-gpl
