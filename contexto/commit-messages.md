# Commit messages útiles

## Cliente Electron completo

Summary:

```txt
Agregar cliente Electron para Piper Neo
```

Description:

```txt
- Agrega la aplicación de escritorio en apps/electron-client.
- Implementa cliente Electron + React + Vite + TypeScript para consumir un servidor local de Piper Neo.
- Agrega onboarding inicial para configurar la URL de API y token opcional.
- Implementa validación de conexión antes de entrar a la aplicación.
- Agrega selección visual de modelos obtenidos desde la API.
- Implementa pantalla de estudio con textarea autoincrementable, generación de audio y reproducción automática.
- Agrega panel de ajustes de síntesis para configurar parámetros del modelo.
- Guarda configuración, modelo seleccionado, texto e historial de audios de forma persistente.
- Agrega historial local de generaciones con opción de reproducir, descargar y limpiar.
- Incluye diseño responsivo, splash inicial, animaciones e iconos locales para uso offline.
- Agrega scripts build.cmd y build.sh para compilar la aplicación.
- Actualiza .gitignore para excluir dependencias y salidas de build de Electron.
- Agrega workflow manual para publicar builds en releases con versionado incremental.
```

## Live chat

Summary:

```txt
Agregar live chat con LLM y voz Piper
```

Description:

```txt
- Agrega ruta Live chat en la aplicación Electron.
- Implementa cliente LLM compatible con endpoints OpenAI /v1.
- Permite configurar URL, token opcional, modelo listado o modelo manual.
- Agrega system prompt, temperatura, max tokens y límite de contexto configurable.
- Conserva mensajes y borrador del chat al cambiar de página.
- Envía el contexto reciente al proveedor LLM para mantener continuidad conversacional.
- Agrega acciones de nuevo chat y regenerar última respuesta del agente.
- Convierte automáticamente la respuesta del agente a audio usando Piper Neo.
- Agrega botón Play debajo de cada respuesta del agente.
- Reproduce automáticamente el último audio generado.
- Integra la voz activa seleccionada en Piper Neo.
- Agrega estilos responsivos, burbujas animadas y panel lateral para configuración del chat.
- Actualiza README principal y README del cliente Electron.
```

## CORS LLM Electron

Summary:

```txt
Corregir conexión LLM en Electron
```

Description:

```txt
- Agrega proxy HTTP en el proceso principal de Electron para evitar bloqueos CORS al usar proveedores LLM externos.
- Expone solicitudes seguras desde preload mediante window.piperNeoDesktop.httpRequest.
- Actualiza el cliente OpenAI-compatible para usar el proxy en Electron y mantener fallback con fetch normal.
- Mejora la normalización de URL base para evitar duplicar /v1.
- Limpia endpoints pegados por error como /models y /chat/completions.
- Evita listar modelos LLM automáticamente mientras se escribe la URL.
- Mantiene carga manual de modelos desde el botón Modelos.
- Agrega ayuda visual para configurar correctamente la URL base compatible con /v1.
```

## Chat streaming + markdown + copiar

Summary:

```txt
Mejorar chat con streaming, markdown y acciones de audio
```

Description:

```txt
- Rediseña el chat con una experiencia más cercana a ChatGPT.
- Agrega streaming real para respuestas LLM compatibles con OpenAI.
- Renderiza markdown, listas, enlaces, inline code y bloques de código.
- Agrega copiado real de bloques de código mediante portapapeles de Electron y fallbacks web.
- Conserva contenido de bloques code para TTS después de limpiar marcadores Markdown.
- Integra iconos lucide-react como dependencia local/offline.
- Mueve la acción Regenerar a la burbuja del agente.
- Reproduce audio por burbuja con animación mientras habla.
```

## UI polish reciente

Summary:

```txt
Pulir interfaz de chat, onboarding y estudio
```

Description:

```txt
- Corrige el footer del chat para que no tape el contenido ni la perilla de scroll.
- Alinea correctamente el botón de enviar con el textarea del composer.
- Mueve acciones de código y vista previa a la zona inferior de la burbuja.
- Agrega onboarding más interactivo con presets y animaciones.
- Agrega transiciones entre pantallas de la aplicación.
- Rediseña la página de estudio para resaltar el textarea y el reproductor.
- Reemplaza el reproductor nativo por controles personalizados con progreso y tiempo.
```
