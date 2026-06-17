# Modo TTS con etiquetas

Piper Neo mantiene el endpoint existente `POST /api/v1/tts` y agrega un modo compatible para guiones multi-voz. Si el campo `text` no contiene etiquetas, el endpoint funciona igual que antes. Si detecta etiquetas como `<model=...>` o `<silence...>`, activa el renderizado por segmentos y genera un solo archivo WAV final.

## Modelo por defecto

El modelo por defecto se define con el campo existente `model`. También se acepta `default_model` como alias más explícito.

```json
{
  "model": "Narrador",
  "text": "Hola, esto lo lee el narrador."
}
```

## Cambiar de modelo dentro del texto

```txt
<model="Elena">Hola, me llamo Elena.</model>

Esto vuelve al modelo por defecto.

<model="Juan">Mucho gusto Elena.
<model="Elena">El placer es mío.
```

Reglas:

- El texto sin etiqueta usa el modelo por defecto.
- `<model="Nombre">` cambia el modelo activo.
- `</model>` limpia el modelo activo y vuelve al modelo por defecto.
- Si no se cierra `</model>`, el último modelo activo se mantiene hasta otro `<model="...">` o hasta el final del texto.
- El nombre debe coincidir con un modelo disponible en `models/`; si no lleva extensión, Piper Neo intenta resolver `.onnx` o `.neo`.

## Speaker y parámetros Piper por bloque

Cada bloque de modelo puede personalizar parámetros de síntesis:

```txt
<model="Elena" speaker="0" length_scale="1.05" noise_scale="0.667" noise_w="0.8" sentence_silence="0.2">
Hola, este bloque usa parámetros personalizados.
</model>
```

Parámetros soportados:

| Parámetro | Uso |
| --- | --- |
| `speaker` | Speaker para modelos multi-speaker. También se acepta `speacker` como alias tolerante. |
| `length_scale` | Controla duración/velocidad de la voz. Menor suele sonar más rápido; mayor, más lento. |
| `noise_scale` | Controla variación durante la inferencia. |
| `noise_w` | Controla variación adicional del modelo. |
| `sentence_silence` | Silencio automático entre frases dentro del bloque, en segundos. |

También se acepta speaker en el nombre del modelo:

```txt
<model="Elena#2">Hola usando speaker 2.</model>
```

Si se usan ambas formas, `speaker="..."` tiene prioridad sobre `Modelo#speaker`.

## Silencios exactos

Los silencios se insertan como audio PCM real, no como puntuación.

Formas soportadas:

```txt
<silence ms="500"/>
<silence sec="1.5"/>
<silence seconds="2"/>
<silence="750ms"/>
<silence="1.2s"/>
```

Internamente todo se convierte a milisegundos.


## Formato PCM y sample rate automático

En guiones multi-voz, Piper Neo no concatena WAVs completos. Cada segmento se convierte a PCM interno, los silencios se generan en PCM real y al final se escribe un único WAV válido.

El `sample_rate` final se elige automáticamente usando el valor más alto entre los modelos usados en el guion. No se agrega ningún parámetro nuevo para esto. Si un segmento fue generado con un `sample_rate` menor, Piper Neo lo normaliza al `sample_rate` final antes de unirlo.

Ejemplo:

```txt
Narrador: 22050 Hz
Elena:    44100 Hz
Juan:     16000 Hz
```

El WAV final se escribe a `44100 Hz`. Los segmentos de 22050 Hz y 16000 Hz se adaptan al formato final para evitar archivos corruptos o cortes incorrectos.

## Ejemplo completo

```json
{
  "default_model": "Narrador",
  "text": "Hoy hablaremos sobre préstamos. <silence ms=\"600\"/> <model=\"Elena\" speaker=\"0\" length_scale=\"1.02\">Hola, soy Elena.</model> Después Juan se acercó y dijo: <model=\"Juan\" speaker=\"1\">Mucho gusto Elena.</model>",
  "return_segments": true
}
```

Respuesta resumida:

```json
{
  "success": true,
  "data": {
    "file": "tts_...wav",
    "url": "/api/v1/files/tts_...wav",
    "format": "wav",
    "markup": {
      "enabled": true,
      "speech_segments": 4,
      "silence_segments": 1,
      "segments": []
    }
  }
}
```

## Compatibilidad

El modo de etiquetas no cambia el contrato anterior del endpoint. Los clientes existentes pueden seguir enviando:

```json
{
  "text": "Texto normal",
  "model": "voz.onnx",
  "speaker_id": 0
}
```

Los campos `noise_scale`, `length_scale`, `noise_w` y `sentence_silence` también se pueden usar en texto normal para aplicar parámetros globales a toda la síntesis.
