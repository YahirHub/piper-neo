# Arquitectura del servidor Piper Neo

## Entrada

El servidor se inicia normalmente así:

```bash
piper --server --models models
```

Si no existe `models/`, se crea. La carpeta puede contener:

- `*.onnx` + `*.onnx.json`
- `*.neo`

## Seguridad

La API queda abierta si no se define token.

Fuentes de token:

1. `--api-token`
2. variable `PIPER_API_TOKEN`
3. `.env`

Si existe token, se acepta:

```http
Authorization: Bearer TOKEN
```

También:

```http
X-API-Token: TOKEN
```

## Gestión de recursos

- Auto-config por CPU al iniciar.
- `--cpu-threads` limita ONNX Runtime por worker.
- `--max-concurrent-jobs` limita jobs activos.
- `--chunk-workers` define workers de chunks.
- `--max-model-replicas` limita réplicas ONNX por modelo.
- `--queue-size` controla cola.
- `--max-temp-bytes` evita llenar disco temporal.

## Scheduler justo

Cada request TTS se divide en chunks. Los chunks se reparten tipo round-robin entre jobs activos:

```text
A1 -> B1 -> C1 -> A2 -> B2 -> C2
```

Así un texto largo no bloquea a textos pequeños.

## Temporales

Durante síntesis se escriben chunks RAW en:

```text
outputs/tmp/{job_id}/chunk_N.raw
```

Luego se ensamblan en un WAV final.

Los temporales se eliminan al finalizar, fallar o cancelar.

## Retención

Los audios generados por API se eliminan después de 1 hora por defecto.

```bash
--output-retention-seconds 3600
```

## Cancelación

Si el cliente cierra conexión:

- el job se marca como cancelado
- no se programan más chunks
- se limpian temporales
- se borra el WAV parcial

La cancelación es cooperativa; no se mata ONNX a media inferencia, se corta en el siguiente punto seguro.
