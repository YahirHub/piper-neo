# Piper Neo Model Manager

Administrador local para preparar modelos fuente de Piper antes de exportarlos al formato `.neo`.

Esta herramienta está pensada para trabajar con carpetas que contienen pares:

```txt
modelo.onnx
modelo.onnx.json
```

El flujo recomendado es:

```txt
1. Seleccionar carpeta de modelos fuente.
2. Revisar y completar metadata del modelo.
3. Agregar imagen base64 si se desea mostrar avatar en clientes Piper Neo.
4. Configurar normalización de texto y reemplazos.
5. Exportar uno o todos los modelos a .neo.
6. Usar los .neo exportados en Piper Neo.
```

Después de exportar a `.neo`, el paquete se considera final. Para modificar metadata, imagen o reemplazos, edita de nuevo el modelo fuente `.onnx + .onnx.json` y vuelve a exportar.

## Instalación

```bash
cd apps/neo-model-manager
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

En Linux/macOS:

```bash
cd apps/neo-model-manager
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python main.py
```


## Ayuda integrada

La aplicación incluye botones de **Ayuda completa** en la ventana principal y en los editores internos. Esa ayuda explica dentro del programa:

```txt
- Qué es un modelo fuente .onnx + .onnx.json.
- Qué significa exportar a .neo.
- Por qué el .neo exportado se considera salida final.
- Qué es metadata del modelo.
- Para qué sirve la imagen base64.
- Qué hacen noise_scale, length_scale, noise_w y sentence_silence.
- Qué es neo.text_normalization.
- Diferencia entre reemplazos manuales y reglas inteligentes.
- Cómo usar prioridad.
- Para qué sirve coincidir palabra completa.
- Para qué sirve distinguir mayúsculas/minúsculas.
- Cómo deberían tratarse decimales, versiones, porcentajes, moneda, URLs y correos.
```

Esto es importante porque Piper Neo agrega capacidades que Piper clásico normalmente no trae como flujo visual: metadata extendida, imagen de modelo, exportación `.neo` y reglas de normalización preparadas por modelo.

## Qué guarda en el JSON

La metadata visible del modelo se guarda en:

```json
{
  "modelcard": {
    "id": "es_MX-Cortana-CE-Legacy.neo",
    "name": "Cortana CE Legacy",
    "description": "Voz clara para narración.",
    "language": "es_MX",
    "voiceprompt": "Voz de narrador profesional.",
    "image": "data:image/png;base64,..."
  }
}
```

La normalización de texto se guarda en el JSON fuente. Al abrir el editor, los cambios se hacen sobre una copia temporal; se escriben en disco solo al presionar Guardar:

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
          "note": "Marca comercial"
        }
      ]
    }
  }
}
```

## Exportación `.neo`

El exportador empaqueta:

```txt
metadata.json
model.onnx
image opcional
```

La imagen base64 se elimina de `metadata.json` durante la exportación y se mueve a una sección binaria `image`, igual que espera Piper Neo.

## Compatibilidad

- Piper clásico puede ignorar los campos `modelcard` y `neo`.
- Piper Neo puede usar esos campos para mostrar metadata, avatar, futuras reglas de normalización y exportación `.neo`.

## Cancelar no guarda cambios

El editor de modelos usa una copia temporal del JSON. Si cambias imagen, metadata o reemplazos y presionas Cancelar, esos cambios no se escriben en el `.onnx.json`. Solo Guardar actualiza el modelo fuente.

## Activación explícita en el core

El core de Piper Neo no altera un JSON clásico automáticamente. La capa se activa cuando existe `neo.text_normalization` o cuando el modelo trae reemplazos legacy en `modelcard.replacements`. Esto evita cambiar el comportamiento de modelos normales que no fueron preparados para Piper Neo.
