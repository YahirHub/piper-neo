# Fecha
2026-06-17

# Objetivo
Ajustar la normalización de texto después de pruebas reales con el modelo `es_MX-Cortana-CE-Legacy`, corrigiendo casos donde moneda, URLs y reemplazos personalizados interactuaban de forma no deseada.

# Decisiones tomadas
- Las salidas generadas por reglas inteligentes de URLs, correos, versiones, moneda, porcentajes y decimales se protegen internamente antes de aplicar reemplazos personalizados.
- Los reemplazos personalizados ya no modifican marcas dentro de URLs normalizadas automáticamente.
- Las URLs recortan puntuación final común para evitar lecturas con puntos extra.
- La regla de moneda acepta sufijos como `pesos`, `peso`, `MXN`, `USD`, `dolares` y `dólares` para evitar duplicar moneda.
- La normalización automática de IPs sigue excluida.

# Arquitectura actual
El flujo de `normalizeTextForSpeech` ahora es:

1. Detectar reglas inteligentes y convertirlas a segmentos protegidos.
2. Dejar el resto del texto normal para reemplazos personalizados.
3. Aplicar reemplazos solo sobre texto no protegido.
4. Restaurar los segmentos protegidos.
5. Entregar el texto final a segmentación y `phonemize`.

# Problemas encontrados
- `$99.50 pesos` podía quedar como `noventa y nueve pesos con cincuenta centavos pesos`.
- `https://github.com` podía recibir el reemplazo `GitHub → Guit Jab` después de ser normalizado como enlace.
- URLs con punto final de oración podían leer un punto extra.
- Algunas siglas como `HTTP`, `HTTPS` y `apt` podían juntarse fonéticamente si se reemplazaban sin comas.

# Soluciones implementadas
- Se agregó protección de segmentos normalizados en `src/cpp/text_normalizer.cpp`.
- Se agregó limpieza de puntuación terminal en URLs.
- Se amplió la regla de moneda para consumir sufijos.
- Se actualizó la documentación de normalización.

# Pendientes
- Seguir ajustando reemplazos por modelo según pruebas auditivas reales.
- Evaluar una opción futura para que el Model Manager pueda marcar reglas como "no aplicar dentro de URLs" si se agregan dominios sin protocolo.
