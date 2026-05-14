# Estado actual útil

## Última base entregada para Electron

Último ZIP entregado antes de este contexto:

- `piper-neo-electron-ui-polish-fix.zip`

Ese ZIP corresponde a la app Electron con mejoras de UI, chat, cache/retry y preview HTML.

## Cambios importantes acumulados en Electron

### Cliente Electron inicial

Estructura principal:

```txt
apps/electron-client/
├─ package.json
├─ index.html
├─ vite.config.ts
├─ tsconfig.json
├─ tsconfig.node.json
├─ README.md
└─ src/
   ├─ main/
   ├─ preload/
   └─ renderer/
```

Incluye:

- Electron + React + Vite + TypeScript.
- Onboarding para configurar URL API de Piper Neo y token opcional.
- Validación de conexión.
- Rutas: `/`, `/setup`, `/models`, `/history`, `/settings`, `/chat`.
- Selector de modelos/voces.
- Pantalla Studio con textarea, ajustes de síntesis, generación y reproducción.
- Historial local en IndexedDB.
- Diseño responsive y usable offline.
- Electron seguro: `contextIsolation`, `nodeIntegration:false`, preload limitado.

### Chat LLM + TTS

Implementado:

- Chat estilo ChatGPT en ruta `/chat`.
- Layout propio, sin sidebar principal.
- Configuración LLM compatible OpenAI `/v1`.
- URL del provider, token opcional, modelo, system prompt, temperature, max tokens y contexto.
- Streaming real de respuestas.
- Markdown renderizado.
- Bloques de código con copiado.
- Preview HTML tipo canvas en ventana Electron maximizada.
- Respuesta del agente convertida a audio con Piper Neo.
- Reproducción por burbuja.
- Botón regenerar dentro de la burbuja del agente, no arriba.
- Indicador visual mientras una burbuja reproduce audio.

### Proxy LLM en Electron

Problema corregido:

- El renderer tenía bloqueo CORS al llamar proveedores LLM externos.

Solución:

- Proxy HTTP en el proceso main de Electron.
- `preload` expone funciones seguras para HTTP/stream.
- `llmApi.ts` usa proxy en Electron y fallback con `fetch` fuera.
- Normalización de URL base para evitar duplicar `/v1`.

### Botón copiar e iconos

Implementado:

- Copiado real de código usando:
  1. Clipboard de Electron vía main.
  2. `navigator.clipboard` como fallback.
  3. textarea temporal como último fallback.
- Iconos migrados a `lucide-react` como dependencia local/bundled, sin CDN.

### Selector de modelo desde chat

Problema corregido:

- Al cambiar voz/modelo desde el chat, el selector mandaba siempre a Estudio.

Solución:

- `modelReturnRoute` recuerda desde dónde se abrió el selector.
- Si se abrió desde `/chat`, al seleccionar modelo regresa a `/chat`.

### Cache de imágenes de modelos

Implementado:

- Cache persistente de imágenes de modelos con IndexedDB.
- Evita recargar imágenes cada vez que abre la app.

### Reintentos

Implementado:

- Reintentos automáticos para peticiones Piper Neo: modelos, imágenes, TTS y audio.
- Reintentos automáticos para proveedor LLM: listado de modelos y chat streaming.
- En streaming se evita reintentar si ya llegó texto parcial, para no duplicar respuesta.

### Último polish UI

Últimos ajustes aplicados:

- Footer del chat ya no queda fixed encima del contenido.
- Scroll/perilla del chat ya no queda tapado por el composer.
- Textarea del chat conserva auto-height sin tapar scrollbar.
- Botón enviar alineado correctamente con textarea.
- Botones “Vista previa” y “Copiar código” movidos abajo junto a Play/Regenerar.
- Botón copiar código copia todos los bloques de código de esa respuesta.
- Vista previa aparece si la respuesta contiene HTML.
- Onboarding más interactivo con presets Local/localhost/Docker.
- Animaciones para onboarding y cambio de pantalla.
- Página Estudio rediseñada para resaltar textarea y reproductor.
- Reproductor personalizado sin controles nativos feos de Electron/Chromium.
