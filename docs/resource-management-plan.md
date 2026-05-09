# Plan de mantenimiento: gestión de recursos para textos largos

## Diagnóstico

El flujo C++ cargaba o acumulaba demasiado trabajo en memoria cuando se enviaban textos muy largos:

1. `--output_file` leía todo `stdin` en un `stringstream` antes de sintetizar, por lo que un texto grande duplicaba memoria desde el inicio.
2. `textToWavFile` generaba todo el audio en un único `std::vector<int16_t>` y solo después escribía el WAV.
3. Una línea o frase extremadamente larga podía llegar completa a fonemización e inferencia ONNX.
4. `SynthesisResult` no inicializaba sus métricas por defecto.
5. Los logs de debug podían imprimir el texto completo, aumentando uso de memoria/terminal con entradas enormes.
6. ONNX Runtime no tenía una opción CLI para limitar hilos de CPU.

## Mejoras implementadas en esta revisión

1. **WAV progresivo**
   - El audio se escribe por bloques durante la síntesis.
   - En archivos seekables se escribe un header provisional y se corrige el tamaño real al finalizar.
   - En stdout/pipes se escribe un header de streaming de mejor esfuerzo y se recomienda `--output_raw` para flujos sin límite.

2. **Entrada larga por streaming**
   - `--output_file` con entrada de texto plano ya no concatena todo `stdin`.
   - Se lee línea por línea y se divide en fragmentos antes de fonemizar/sintetizar.

3. **Chunking seguro para UTF-8**
   - Se agregó división por `--max-text-chunk-bytes`.
   - Intenta cortar en puntuación o espacios.
   - Si no hay separadores, retrocede hasta un límite UTF-8 válido.

4. **Control de CPU**
   - Se agregó `--cpu-threads NUM` para limitar hilos de ONNX Runtime.
   - Útil para servidores pequeños o cuando el motor convive con APIs/webhooks.

5. **Métricas inicializadas**
   - `SynthesisResult` ahora inicia en cero para evitar valores basura o acumulados inesperados.

6. **Logs seguros**
   - Los logs de texto largo ahora muestran solo una vista previa y el tamaño total en bytes.

## Uso recomendado

Para generar un WAV largo sin saturar memoria:

```bash
cat texto_largo.txt | ./piper \
  --model voz.onnx \
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

## Siguientes mejoras sugeridas

1. **Límite por inferencia ONNX**
   - Agregar `--max-phoneme-ids` para dividir frases que produzcan demasiados IDs aun después del chunking por texto.

2. **Backpressure real en `--output_raw`**
   - Cambiar el buffer compartido por una cola acotada para que el sintetizador espere si stdout está lento.

3. **Límites de memoria configurables**
   - Agregar `--max-audio-buffer-mb` y fallback a streaming obligatorio.

4. **Modo servidor seguro**
   - Si se usa en API, envolver la síntesis en una cola de trabajos con concurrencia fija: por ejemplo, máximo 1-2 inferencias simultáneas por modelo.

5. **Pruebas de estrés**
   - Añadir tests que generen entradas de 100 KB, 1 MB y frases sin espacios para validar que la RAM permanezca estable.
