# Filtro y sanitizado de texto para TTS

Piper Neo ahora protege el texto antes de enviarlo al fonemizador y al modelo de voz. La meta no es censurar contenido, sino convertir entradas ruidosas en texto pronunciable para evitar balbuceos, cortes raros o lecturas ininteligibles.

## Capas implementadas

1. **Cliente Electron**
   - Sanitiza antes de llamar a `/api/v1/tts`.
   - Muestra en Estudio una vista previa del texto final que se enviará.
   - Muestra advertencias cuando detecta URLs, HTML, Markdown, BBCode, código, emoji, cadenas técnicas, Unicode raro o repeticiones extremas.
   - Guarda metadatos de preprocesado en el audio de chat e historial.

2. **Servidor C++ `/api/v1/tts`**
   - Aplica una segunda capa obligatoria para proteger cualquier cliente externo.
   - Valida UTF-8 de forma estricta.
   - Elimina controles, caracteres invisibles, BOM, variation selectors y marcas de dirección.
   - Normaliza puntuación común, full-width ASCII y ligaduras frecuentes.
   - Convierte HTML/BBCode/Markdown a texto plano.
   - Resume URLs, correos, código, hashes/base64 y emoji.
   - Colapsa repeticiones y espacios.
   - Rechaza con `422 text_not_pronounceable` si el texto queda vacío o no es seguro.

## Respuesta de la API

`POST /api/v1/tts` agrega el campo opcional `text_preprocessing`:

```json
{
  "text_preprocessing": {
    "speakText": "Texto final enviado al motor de voz",
    "warnings": ["URL_SUMMARIZED", "MARKUP_STRIPPED"],
    "riskScore": 0.28,
    "stats": {
      "rawChars": 120,
      "speakChars": 84,
      "rawBytes": 130,
      "speakBytes": 88,
      "urls": 1,
      "emails": 0,
      "codeBlocks": 0,
      "emojis": 0
    }
  }
}
```

## Ajustes disponibles en Electron

En **Ajustes → Filtro de texto para voz**:

- **Sanitizar texto antes de la API**: deja activo el filtro del cliente. Aunque se desactive, el servidor sigue protegiendo la API.
- **Modo normal**: recomendado. Resume enlaces, código y ruido técnico.
- **Modo literal**: conserva más contenido, pero sigue reparando Unicode y texto peligroso.
- **Límite seguro de caracteres**: valor inicial 5000.

## Métricas

`GET /api/v1/metrics` incluye:

```json
{
  "text_preprocessing": {
    "sanitized_inputs": 0,
    "sanitize_warnings": 0,
    "rejected_inputs": 0
  }
}
```

Estas métricas permiten detectar entradas problemáticas sin guardar el texto crudo del usuario.
