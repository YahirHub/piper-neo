# Fecha
2026-06-17

# Objetivo
Corregir el comportamiento de pausas en Piper Neo cuando el texto contiene puntos, signos de cierre o saltos de línea. El objetivo es que narraciones como `Hola amigo.\nComo estás?` no se lean corridas y generen una pausa perceptible entre frases o líneas.

# Decisiones tomadas
- Se decidió resolverlo en el core de Piper Neo, no en Electron.
- Se decidió no depender únicamente de `piper-phonemize`/eSpeak para interpretar pausas por puntuación.
- Se agregó separación explícita de texto en segmentos antes de sintetizar.
- Se consideran pausas fuertes los caracteres `.`, `!`, `?` y `…`.
- Los saltos de línea se tratan como pausas intencionales aunque no exista punto al final de la línea.
- Los saltos de línea consecutivos se colapsan para evitar segmentos vacíos.
- El silencio por oración por defecto se ajustó a `0.35s` para que sea perceptible en narraciones largas.
- Se evita cortar casos comunes donde el punto no representa cierre de oración, como decimales, versiones y abreviaturas frecuentes.

# Arquitectura actual
El flujo de síntesis del core ahora puede segmentar el texto en frases o bloques antes de generar el audio. Cada segmento hablado se sintetiza por separado y entre segmentos se inserta silencio PCM real usando el valor de `sentenceSilenceSeconds`.

La lógica se mantiene en el core para que funcione igual desde CLI, API y cliente Electron sin duplicar reglas en la interfaz.

# Librerías usadas
No se agregaron librerías nuevas.

# Archivos importantes modificados
- `src/cpp/piper.cpp`
- `src/cpp/piper.hpp`
- `src/cpp/main.cpp`

# Problemas encontrados
- Piper/eSpeak respetaba parcialmente las comas, pero los puntos podían no producir una pausa clara.
- Textos con salto de línea podían leerse corridos si la puntuación o el fonemizador no generaban separación suficiente.
- Aumentar pausas únicamente desde el texto no era confiable porque dependía del modelo y del fonemizador.

# Soluciones implementadas
- Segmentación explícita de oraciones antes de la síntesis.
- Inserción de silencios PCM reales entre segmentos.
- Soporte para pausas por saltos de línea.
- Colapso de saltos de línea consecutivos.
- Protección para evitar cortes incorrectos en decimales, versiones y abreviaturas comunes como `Dr.`, `Dra.`, `Sr.`, `Sra.`, `Lic.`, `Ing.`, `etc.`.
- Actualización del valor de silencio por oración por defecto de `0.2s` a `0.35s`.
- Actualización de ayuda CLI para reflejar el nuevo valor por defecto.

# Pendientes
- Probar con varios modelos en español para confirmar que `0.35s` se siente natural.
- Evaluar si se necesita una pausa mayor para doble salto de línea en una futura mejora.
- Considerar una configuración independiente para pausa por línea y pausa por oración si el usuario lo solicita.
- Agregar pruebas automatizadas de segmentación si el proyecto incorpora suite de tests para el core.

# Próximos pasos
- Compilar localmente el core.
- Probar textos cortos y largos con puntos, signos de interrogación, exclamación y saltos de línea.
- Hacer commit únicamente con los archivos del core y este archivo de contexto.
