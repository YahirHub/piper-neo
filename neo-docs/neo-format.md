# Formato `.neo` v1

`.neo` es un contenedor binario propio de Piper Neo. No es ZIP. Su objetivo es distribuir una voz como un único archivo:

- metadata JSON compatible con `*.onnx.json`
- modelo ONNX interno
- imagen opcional
- información de modelcard, speakers, idioma, parámetros de inferencia y fonemas

## Header

```text
magic:    8 bytes  "PIPERNEO"
version:  u32 LE   actualmente 1
sections: u32 LE   número de secciones
```

## Directorio de secciones

Cada sección guarda:

```text
name_len:          u32 LE
name:              bytes UTF-8
content_type_len:  u32 LE
content_type:      bytes UTF-8
compression:       u32 LE  0=none, 1=zstd
uncompressed_size: u64 LE
stored_size:       u64 LE
offset:            u64 LE
```

## Secciones v1

### `metadata.json`

JSON UTF-8. Conserva la estructura Piper clásica, pero sin `modelcard.image` si la imagen fue extraída a la sección `image`.

Incluye además:

```json
{
  "piper_neo": {
    "format": "piper-neo",
    "format_version": 1,
    "model_section": "model.onnx",
    "compression": "zstd"
  }
}
```

### `model.onnx`

Modelo ONNX original comprimido con zstd. La compresión es sin pérdida.

### `image`

Opcional. Imagen binaria decodificada desde `modelcard.image` o pasada con `--neo-image`.

`content_type` puede ser:

- `image/jpeg`
- `image/png`
- `image/webp`

## Flujo de carga

1. El usuario pasa `--model voz.neo` o la API recibe `model: "voz.neo"`.
2. `neo_model.cpp` inspecciona el paquete.
3. Se extrae a caché temporal:
   - `model.onnx`
   - `model.onnx.json`
4. Piper carga el ONNX extraído con el flujo actual.

Esto mantiene compatibilidad con ONNX Runtime y evita reescribir el motor de inferencia.

## Ventajas

- Un solo archivo por voz.
- Menos riesgo de perder el `.onnx.json`.
- Menor peso en disco/distribución por zstd.
- Metadata e imagen integradas.
- Compatible con el runtime actual.

## Limitación conocida

Al cargar un `.neo`, el modelo se descomprime a disco temporal. Esto conserva calidad y compatibilidad, pero implica un costo inicial de extracción. El cache evita repetir la extracción si el paquete ya fue preparado.
