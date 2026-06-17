# Fecha
2026-06-17

# Objetivo
Agregar una herramienta local en Python/PySide6 para administrar modelos fuente de Piper Neo antes de exportarlos a `.neo`. La herramienta debe permitir seleccionar una carpeta con modelos `.onnx`, editar metadata, configurar imagen base64, preparar reglas de normalización/reemplazo y exportar uno o todos los modelos a formato `.neo`.

# Decisiones tomadas
- Se creó una app separada en `apps/neo-model-manager` para no mezclar esta herramienta con el cliente Electron.
- Se decidió usar PySide6 en lugar de Tkinter/PyQt5 para una base más moderna y mantenible.
- La herramienta trabaja únicamente con modelos fuente `.onnx + .onnx.json`.
- Los paquetes `.neo` exportados se consideran salida final y no se editan desde el administrador.
- La imagen del modelo se guarda como `modelcard.image` en base64 dentro del JSON fuente y se mueve a una sección binaria `image` al exportar `.neo`.
- La configuración de normalización se guarda bajo `neo.text_normalization` para no contaminar campos originales de Piper.
- Los reemplazos se guardan como objetos con `from`, `to`, `case_sensitive`, `whole_word`, `priority` y `note`.
- Se incluyó migración tolerante desde el formato legacy `modelcard.replacements` usado por administradores anteriores.
- Se agregó ayuda integrada dentro de la aplicación para explicar metadata, imágenes, parámetros Piper, normalización, reemplazos, prioridad, palabra completa, mayúsculas/minúsculas y exportación `.neo`.
- La exportación `.neo` desde Python usa secciones sin compresión para evitar depender de zstd en el entorno de escritorio. El core de Piper Neo ya soporta secciones sin compresión.

# Arquitectura actual
La app queda organizada en módulos:

- `apps/neo-model-manager/main.py`: entrada de ejecución.
- `apps/neo-model-manager/neo_model_manager/app.py`: interfaz PySide6, edición de modelos, imagen, reemplazos y exportación.
- `apps/neo-model-manager/neo_model_manager/model_store.py`: escaneo de modelos, lectura/escritura JSON, defaults, SHA256, base64 y migración de reemplazos.
- `apps/neo-model-manager/neo_model_manager/neo_package.py`: escritor de paquetes `.neo` compatible con el formato `PIPERNEO` versión 1.
- `apps/neo-model-manager/README.md`: documentación de uso, ayuda integrada y estructura de metadata.
- `apps/neo-model-manager/requirements.txt`: dependencia PySide6.

# Librerías usadas
- Python 3.
- PySide6 para interfaz gráfica.
- Librerías estándar de Python: `json`, `base64`, `hashlib`, `mimetypes`, `struct`, `pathlib`, `dataclasses`.

# Archivos importantes modificados
- `apps/neo-model-manager/main.py`
- `apps/neo-model-manager/requirements.txt`
- `apps/neo-model-manager/README.md`
- `apps/neo-model-manager/neo_model_manager/__init__.py`
- `apps/neo-model-manager/neo_model_manager/app.py`
- `apps/neo-model-manager/neo_model_manager/model_store.py`
- `apps/neo-model-manager/neo_model_manager/neo_package.py`
- `contexto/09-neo-model-manager.md`

# Problemas encontrados
- Los administradores anteriores usaban Tkinter/ttkbootstrap o PyQt5 y guardaban reemplazos en `modelcard.replacements`, que no separa claramente metadata del modelo y reglas de normalización.
- Piper Neo todavía necesita una capa de core que lea `neo.text_normalization` antes de phonemize para aplicar esos reemplazos durante la síntesis.
- Los reemplazos simples pueden romper casos técnicos si se aplican sin proteger URLs, correos o versiones.
- Editar paquetes `.neo` directamente complicaría el flujo y podría generar inconsistencias, porque `.neo` es un contenedor final.

# Soluciones implementadas
- App nueva con escaneo de pares `.onnx + .onnx.json`.
- Tabla visual con imagen, archivo, ID, nombre, idioma, estado de ONNX, normalización y conteo de reemplazos.
- Editor de metadata: ID, nombre, idioma, descripción, voice prompt, SHA256 y parámetros Piper por defecto.
- Editor de imagen: agregar/cambiar/eliminar imagen base64.
- Editor de normalización: activar/desactivar, locale, flags inteligentes y tabla CRUD de reemplazos.
- Reemplazos recomendados para marcas comunes como `Amazon Prime`, `Facebook`, `Prime` y `YouTube`.
- Exportación individual o masiva a `.neo`.
- Documentación local dentro de la app, botón de ayuda completa y README.

# Pendientes
- Revisar con usuarios reales si la ayuda integrada necesita capturas o ejemplos guiados paso a paso.
- Conectar el core de Piper Neo para leer `neo.text_normalization` antes de `phonemize`.
- Implementar normalización inteligente real de decimales, versiones, porcentajes, monedas, URLs y correos.
- Evaluar si conviene agregar compresión zstd opcional al exportador Python.
- Agregar pruebas automatizadas para el escritor `.neo` comparando extracción con el core C++.
- Considerar empaquetado con PyInstaller para distribuir el administrador en Windows.

# Próximos pasos
- Ejecutar la app localmente con PySide6.
- Probar una carpeta real de modelos `.onnx + .onnx.json`.
- Editar metadata e imagen de un modelo fuente.
- Agregar reglas de reemplazo y verificar que queden en `neo.text_normalization`.
- Exportar un `.neo` y probar que Piper Neo lo cargue correctamente.
- Implementar después la lectura de reemplazos en el core TTS para que la configuración tenga efecto durante la síntesis.


## Revisión adicional antes de commit

- El editor de modelos ahora trabaja con una copia temporal del JSON.
- Cancelar descarta cambios de imagen, metadata y reemplazos.
- Guardar copia los cambios al registro real y luego actualiza el `.onnx.json`.
- La edición de reemplazos valida duplicados también al modificar una regla existente.
- La sección de reglas inteligentes se renombró como reglas incluidas, no futuras.
