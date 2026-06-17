# Fecha
2026-06-17

# Objetivo
Implementar en el core de Piper Neo la lectura y aplicación de `neo.text_normalization` desde el `.onnx.json` o desde el `metadata.json` de un paquete `.neo`. El objetivo es mejorar la pronunciación antes de `phonemize`, aplicando reemplazos personalizados y reglas inteligentes para decimales, versiones, porcentajes, moneda, URLs y correos.

# Decisiones tomadas
- La normalización se implementa en el core, no en Electron ni en el Model Manager.
- Se creó una capa modular en `src/cpp/text_normalizer.hpp` y `src/cpp/text_normalizer.cpp`.
- La configuración oficial queda en `neo.text_normalization` para evitar mezclar campos propios con Piper clásico.
- Se mantiene compatibilidad con el formato legacy `modelcard.replacements` usado por administradores anteriores.
- La normalización se aplica antes de la segmentación por puntos/saltos de línea y antes de `phonemize`.
- Para evitar doble procesamiento al segmentar internamente oraciones, se desactiva temporalmente la normalización durante las llamadas recursivas de síntesis.
- Las reglas personalizadas se ordenan por prioridad descendente y longitud descendente para procesar primero frases completas.
- Los decimales se convierten a forma hablada con `punto`, por ejemplo `3.5` → `tres punto cinco`.

# Arquitectura actual
El flujo de síntesis queda así:

1. Se carga el modelo y su JSON con `loadVoice`.
2. `parseTextNormalizationConfig` lee `neo.text_normalization` y `modelcard.replacements` legacy.
3. `textToAudio` recibe el texto.
4. Si la normalización está habilitada, `normalizeTextForSpeech` transforma el texto.
5. Después se aplica la segmentación por puntuación/saltos de línea.
6. El texto normalizado llega a `phonemize`.
7. Piper genera audio con el modelo ONNX.

# Librerías usadas
No se agregaron librerías externas. Se usan utilidades estándar de C++17: `regex`, `algorithm`, `sstream`, `string`, `vector` y `cctype`.

# Archivos importantes modificados
- `CMakeLists.txt`
- `src/cpp/piper.hpp`
- `src/cpp/piper.cpp`
- `src/cpp/text_normalizer.hpp`
- `src/cpp/text_normalizer.cpp`
- `docs/text-normalization.md`
- `contexto/10-normalizacion-texto-piper-neo.md`

# Problemas encontrados
- Piper/eSpeak podía leer `3.5` como `tres cinco` o `10.25` como `diez veinticinco` porque el punto decimal no siempre se conserva como palabra hablada.
- `phoneme_map` no sirve para corregir frases completas como `Facebook` o `Amazon Prime`, porque opera a nivel fonema y no a nivel texto.
- Un reemplazo simple sin reglas puede romper URLs, correos, versiones o palabras que solo contienen una coincidencia parcial.
- La segmentación por puntos debe recibir texto ya normalizado para no confundir decimales con cierres de oración.

# Soluciones implementadas
- Capa de normalización previa a `phonemize`.
- Lectura de configuración desde `neo.text_normalization`.
- Compatibilidad con `modelcard.replacements` legacy.
- Reemplazos personalizados con `from`, `to`, `case_sensitive`, `whole_word`, `priority` y `note`.
- Normalización de decimales, porcentajes, moneda, versiones, URLs y correos.
- Conversión básica de números a palabras en español.
- Documentación en `docs/text-normalization.md`.

# Pendientes
- Probar con modelos reales de distintos países para ajustar naturalidad de números largos.
- Evaluar reglas específicas por locale en el futuro, por ejemplo `es-MX`, `es-AR` o `en-US`.
- Agregar pruebas unitarias dedicadas cuando el proyecto tenga una suite estable para core.
- Evaluar normalización más avanzada para moneda según contexto: MXN, USD, EUR.
- Permitir que el Model Manager genere presets por idioma.

# Próximos pasos
- Compilar localmente el core.
- Probar textos con `3.5`, `10.25`, `10.25%`, `$10.25`, `v1.0.3`, correos y URLs.
- Exportar un modelo `.neo` desde `apps/neo-model-manager` con reglas personalizadas.
- Confirmar que Piper Neo use esas reglas tanto desde `.onnx + .json` como desde `.neo`.
