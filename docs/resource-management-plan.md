# Plan de mantenimiento: gestión de recursos para textos largos

## Diagnóstico del código original

El flujo C++ cargaba o acumulaba demasiado trabajo en memoria cuando se enviaban textos muy largos:

1. `--output_file` leía todo `stdin` en un `stringstream` antes de sintetizar, por lo que un texto grande duplicaba memoria desde el inicio.
2. `textToWavFile` generaba todo el audio en un único `std::vector<int16_t>` y solo después escribía el WAV.
3. Una línea o frase extremadamente larga podía llegar completa a fonemización e inferencia ONNX.
4. `SynthesisResult` no inicializaba sus métricas por defecto.
5. Los logs de debug podían imprimir el texto completo, aumentando uso de memoria/terminal con entradas enormes.
6. ONNX Runtime no tenía una opción CLI para limitar hilos de CPU.
7. En modo API, si el cliente cerraba la conexión, el servidor podía seguir generando un audio que nadie iba a recibir.

## Mejoras implementadas

### 1. WAV progresivo

- El audio se escribe por bloques durante la síntesis.
- En archivos seekables se escribe un header provisional y se corrige el tamaño real al finalizar.
- En stdout/pipes se escribe un header de streaming de mejor esfuerzo y se recomienda `--output_raw` para flujos sin límite.
- Ya no se necesita tener todo el WAV en RAM antes de guardarlo.

### 2. Entrada larga por streaming

- `--output_file` con entrada de texto plano ya no concatena todo `stdin`.
- Se lee línea por línea y se divide en fragmentos antes de fonemizar/sintetizar.
- `--input_file` permite leer un `.txt` directamente sin depender del shell.

### 3. Chunking inteligente y seguro para UTF-8

El divisor intenta respetar este orden:

1. Preguntas/exclamaciones españolas completas: `¿...?`, `¡...!`.
2. Párrafos.
3. Frases terminadas en `.`, `?`, `!` o `…`.
4. Pausas suaves: `,`, `;`, `:`.
5. Espacios en blanco.
6. Como último recurso, corte seguro en frontera UTF-8.

Esto evita romper acentos, `ñ`, `¿`, `¡` y palabras normales. Si una palabra o frase es exageradamente grande, corta en un punto seguro para proteger RAM/CPU.

### 4. Control de CPU

- Se agregó `--cpu-threads NUM` para limitar hilos de ONNX Runtime.
- Internamente se configura `SetIntraOpNumThreads(NUM)` y `SetInterOpNumThreads(1)`.
- También se desactivan optimizaciones de memoria que pueden incrementar picos en algunos escenarios: CPU mem arena, memory pattern y profiling.

### 5. Concurrencia controlada en API

- El servidor permite varias síntesis simultáneas con `--max-concurrent-jobs`.
- Para solicitudes del mismo modelo, usa réplicas ONNX controladas por `--max-model-replicas`.
- Si se alcanza el límite global o el límite de réplicas, responde HTTP 429 con `server_busy`.
- Esto permite paralelismo real sin dejar que textos largos creen procesos/hilos sin control ni saturen RAM/CPU.

### 6. Cancelación por desconexión del cliente

- En `POST /api/v1/tts`, el servidor revisa si el socket del cliente sigue conectado.
- La revisión ocurre antes de iniciar y entre chunks/frases/callbacks de escritura.
- Si el cliente cerró la conexión, se cancela la síntesis, se borra el WAV parcial y se libera el mutex.
- No se interrumpe agresivamente una llamada individual de ONNX a media inferencia; la cancelación ocurre en el siguiente punto seguro.

### 7. Métricas inicializadas

- `SynthesisResult` ahora inicia en cero para evitar valores basura o acumulados inesperados.

### 8. Logs seguros

- Los logs de texto largo ahora muestran solo una vista previa y el tamaño total en bytes.

## Uso recomendado CLI

Para generar un WAV largo sin saturar memoria:

```bash
./piper \
  --model voz.onnx \
  --input_file texto_largo.txt \
  --output_file salida.wav \
  --max-text-chunk-bytes 4096 \
  --cpu-threads 2
```

Para streaming sin header WAV rígido:

```bash
cat texto_largo.txt | ./piper \
  --model voz.onnx \
  --output_raw \
  --cpu-threads 2 > salida.raw
```

## Uso recomendado API protegida

`.env`:

```env
PIPER_API_TOKEN=mi-token-largo-y-seguro
```

Servidor:

```bash
./piper \
  --server \
  --models models \
  --host 127.0.0.1 \
  --port 8080 \
  --cpu-threads 2 \
  --max-text-chunk-bytes 4096 \
  --max-concurrent-jobs 2 \
  --max-model-replicas 2
```

Petición:

```bash
curl -X POST http://127.0.0.1:8080/api/v1/tts \
  -H "Authorization: Bearer mi-token-largo-y-seguro" \
  -H "Content-Type: application/json" \
  -d '{"model":"es_MX-Veritasium.onnx","text":"Hola, ¿cómo estás?","output_file":"demo.wav"}'
```

## Siguientes mejoras sugeridas

1. **Límite por inferencia ONNX**
   - Agregar `--max-phoneme-ids` para dividir frases que produzcan demasiados IDs aun después del chunking por texto.

2. **Backpressure real en `--output_raw`**
   - Cambiar el buffer compartido por una cola acotada para que el sintetizador espere si stdout está lento.

3. **Límites de memoria configurables**
   - Agregar `--max-audio-buffer-mb` y fallback a streaming obligatorio.

4. **Cola de trabajos opcional**
   - Permitir `--queue-size N` para encolar trabajos en vez de devolver `server_busy` inmediatamente.

5. **Pruebas de estrés**
   - Añadir tests que generen entradas de 100 KB, 1 MB y frases sin espacios para validar que la RAM permanezca estable.
