# Normalización de texto en Piper Neo

Piper Neo agrega una capa de normalización antes de `phonemize`. Esta capa prepara el texto para que el motor TTS pronuncie mejor marcas, números y texto técnico.

## Flujo

```txt
texto original
↓
normalización Piper Neo
↓
segmentación por puntos / saltos de línea
↓
phonemize / eSpeak
↓
síntesis ONNX
```

La normalización se aplica solo al texto que será leído. En guiones con etiquetas `<model>` y `<silence>`, la API separa primero los segmentos y después cada segmento hablado pasa por el modelo correspondiente.

## Configuración por modelo

La configuración vive dentro del JSON del modelo fuente o dentro del `metadata.json` exportado en un `.neo`:

```json
{
  "neo": {
    "text_normalization": {
      "enabled": true,
      "locale": "es-MX",
      "builtin": {
        "decimals": true,
        "versions": true,
        "percentages": true,
        "currency": true,
        "urls": true,
        "emails": true
      },
      "replacements": [
        {
          "from": "Amazon Prime",
          "to": "Amazon Praim",
          "case_sensitive": false,
          "whole_word": true,
          "priority": 100,
          "note": "Marca completa antes que palabra suelta"
        }
      ]
    }
  }
}
```

## Reemplazos personalizados

Los reemplazos se ordenan por `priority` y después por longitud. Esto permite que `Amazon Prime` se procese antes que `Prime`.

- `from`: texto original.
- `to`: texto que se enviará a phonemize.
- `case_sensitive`: si `true`, respeta mayúsculas/minúsculas exactamente.
- `whole_word`: si `true`, evita reemplazar dentro de otras palabras.
- `priority`: mayor número se aplica primero.
- `note`: documentación para humanos.

## Reglas inteligentes incluidas

Piper Neo incluye normalización básica para español:

```txt
3.5             → tres punto cinco
10.25%         → diez punto veinticinco por ciento
$10.25         → diez pesos con veinticinco centavos
v1.0.3          → versión uno punto cero punto tres
correo@x.com    → correo arroba x punto com
https://x.com   → enlace x punto com
```


## Compatibilidad legacy

Piper Neo también lee el formato anterior usado por administradores viejos:

```json
{
  "modelcard": {
    "replacements": [
      ["Facebook", "Feisbuk"]
    ]
  }
}
```

Ese formato funciona, pero el recomendado es `neo.text_normalization.replacements`.

## Recomendaciones

- Usa reemplazos para marcas y palabras que el modelo pronuncie mal.
- Usa `whole_word=true` casi siempre.
- Usa `case_sensitive=false` para marcas como Facebook, YouTube o TikTok.
- Usa prioridad alta para frases completas.
- No edites `.neo` exportados; edita el `.onnx.json` fuente y exporta de nuevo.
