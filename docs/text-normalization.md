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

La configuración vive dentro del JSON del modelo fuente o dentro del `metadata.json` exportado en un `.neo`. Piper Neo solo activa esta capa cuando el modelo declara `neo.text_normalization` o cuando trae reemplazos legacy en `modelcard.replacements`; un JSON clásico sin esos campos no se modifica de forma automática:

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
10.25%         → 10 punto 25 por ciento
$10.25         → 10 punto 25 pesos
v1.0.3          → versión uno punto cero punto tres
correo@x.com    → correo arroba x punto com
https://x.com   → enlace x punto com
```


## Compatibilidad legacy

Piper Neo también lee el formato anterior usado por administradores viejos. En ese caso se aplican los reemplazos, pero no se activan reglas inteligentes si el JSON no tiene `neo.text_normalization`:

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

## Protección de tokens normalizados

Las reglas inteligentes generan texto protegido internamente antes de aplicar reemplazos personalizados. Esto evita que una marca dentro de un enlace se vuelva a reemplazar por accidente.

Por ejemplo, si existe el reemplazo `GitHub → Guit Jab`, el texto `https://github.com` se normaliza como `enlace github punto com` y no como `enlace Guit Jab punto com`. Los reemplazos siguen funcionando fuera de enlaces y correos.

También se recorta puntuación final común en URLs, para que `https://youtube.com.` no se lea con un `punto` extra del final de la oración.

## Moneda con sufijo

La regla de moneda consume sufijos comunes para evitar duplicados:

```txt
$99.50 pesos  → 99 punto 50 pesos
USD 10.25     → 10 punto 25 dólares
```

Así se evita que `$99.50 pesos` termine duplicando unidades, y también se evita inventar `centavos` cuando el usuario escribió un decimal monetario.

## Auditoría de seguridad de aplicación

El administrador de modelos trabaja sobre una copia temporal mientras el usuario edita un modelo. Si se presiona Cancelar, se descartan cambios de metadata, imagen y reemplazos. Solo al presionar Guardar se copia el contenido de vuelta al registro y se escribe el `.onnx.json`.

La normalización tampoco muta el estado compartido de la voz durante síntesis. Las llamadas recursivas internas para insertar pausas reciben el texto ya normalizado y no desactivan flags globales, evitando interferencias entre solicitudes concurrentes del servidor.
