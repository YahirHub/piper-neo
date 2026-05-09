# Gestión de CPU/RAM y plan de mantenimiento

## Problema original

El flujo original podía colapsar con textos extremadamente largos porque trabajaba demasiado de una sola vez:

1. Leía texto completo en memoria.
2. Generaba audio en un único buffer grande.
3. No limitaba suficientemente hilos de CPU.
4. Un texto gigante podía monopolizar la inferencia.
5. En API, si el cliente cerraba conexión, el proceso podía seguir generando audio innecesario.

## Gestión actual de RAM

El servidor evita picos grandes de memoria con estas reglas:

- Divide texto en chunks inteligentes.
- No corta UTF-8 ni caracteres como `á`, `ñ`, `¿`, `¡`.
- Escribe cada chunk a un temporal RAW.
- Une los temporales en orden al WAV final.
- Borra `outputs/tmp/<job_id>/` al terminar, fallar o cancelar.
- Limita el tamaño de entrada con `--max-input-bytes`.
- Limita el tamaño preferido de chunk con `--max-text-chunk-bytes`.
- Limita réplicas ONNX con `--max-model-replicas`.

## Gestión actual de CPU

El servidor usa auto-configuración por defecto:

```bash
./piper --server --models models
```

Eso aplica `--cpu-profile balanced` automáticamente. El servidor calcula:

- Hilos disponibles del hardware.
- Hilos ONNX por worker.
- Número de workers de chunks.
- Máximo de trabajos activos.
- Máximo de réplicas por modelo.
- Tamaño de cola.

No usa todos los hilos del CPU en cada solicitud, porque eso causa oversubscription. En su lugar reparte pocos hilos por worker.

## Scheduler justo

Cada request se convierte en un job. Cada job se divide en chunks:

```txt
A: A1 A2 A3 A4
B: B1 B2
C: C1 C2 C3
```

El scheduler procesa en round-robin:

```txt
A1 → B1 → C1 → A2 → B2 → C2 → A3 → C3 → A4
```

Así un texto enorme no bloquea a textos pequeños.

## Concurrencia

La concurrencia se controla con:

```bash
--max-concurrent-jobs
--chunk-workers
--max-model-replicas
--queue-size
--queue-timeout-seconds
--cpu-threads
```

Recomendación para CPU de 8 hilos:

```bash
./piper --server --models models \
  --cpu-profile balanced
```

Override manual:

```bash
./piper --server --models models \
  --cpu-threads 2 \
  --max-concurrent-jobs 3 \
  --chunk-workers 3 \
  --max-model-replicas 2 \
  --queue-size 32
```

## Cancelación

Si el cliente cierra conexión:

- El job se marca como cancelado.
- Ya no se programan más chunks.
- Se espera a que termine el chunk actual en ONNX.
- Se borran temporales y WAV parcial.
- Se libera la réplica del modelo.

## Metadata de modelos

`/api/v1/models` lee los `.onnx.json` y devuelve metadata útil:

- `audio.sample_rate`
- `audio.quality`
- `espeak.voice`
- `language.code`
- `inference.noise_scale`
- `inference.length_scale`
- `inference.noise_w`
- `num_speakers`
- `piper_version`
- `modelcard` sin imagen base64

La imagen se sirve aparte:

```txt
GET /api/v1/models/{model}/image
```

## Próximas mejoras sugeridas

1. Cache LRU de modelos con `--max-loaded-models` y `--model-idle-ttl-seconds`.
2. Cache de fonemas para frases repetidas.
3. Endpoint async con job id para audios muy largos.
4. Límites por duración estimada de audio.
5. Tests de estrés con 5, 10 y 20 clientes concurrentes.
6. Benchmarks automáticos por perfil CPU.
