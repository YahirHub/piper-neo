# Piper Neo Electron Client

Cliente de escritorio local para usar Piper Neo API Server con una interfaz moderna, responsiva y 100% offline en runtime.

## Características

- Onboarding para configurar URL de API y token opcional.
- Check de conexión con manejo de errores: servidor sin respuesta, token inválido o endpoint incompatible.
- Rutas internas persistentes: `/`, `/setup`, `/models`, `/chat`, `/history`, `/settings`.
- Estado persistente en `localStorage`: URL, token, modelo activo, speaker ID, texto en edición y preferencias.
- Historial de audios en `IndexedDB`, con reproducción, descarga y limpieza local.
- Listado de modelos usando `GET /api/v1/models?include=metadata`.
- Carga de imágenes del modelo usando `GET /api/v1/models/{model}/image`.
- Generación de audio usando `POST /api/v1/tts` y descarga automática desde `GET /api/v1/files/{file}`.
- Iconos offline definidos como SVG locales en `src/renderer/lib/icons.tsx`; no usa CDN.
- Live chat con proveedores LLM compatibles con OpenAI `/v1`, system prompt, contexto, nuevo chat, regeneración y reproducción con Piper Neo.
- UI responsive con layout de escritorio, tablet y móvil.

## Requisitos

- Node.js 20+
- npm
- Piper Neo server local activo, por ejemplo:

```bash
./piper --server --models models
```

Con token opcional:

```bash
./piper --server --models models --api-token "secret"
```

## Desarrollo

```bash
cd apps/electron-client
npm install
npm run dev
```

## Build

```bash
cd apps/electron-client
npm run build
npm run package
```

Para instaladores/distribuibles:

```bash
npm run dist
```

## Notas de seguridad

El cliente usa Electron con `contextIsolation`, `nodeIntegration: false` y preload limitado. No carga recursos remotos para la UI; solo se conecta a la URL local/remota configurada de Piper Neo API.

## Scripts de build locales

Windows:

```bat
build.cmd
```

Linux:

```bash
./build.sh
```

Los scripts validan que exista Node/npm, instalan dependencias si falta `node_modules`, limpian `dist/` y `release/`, ejecutan `npm run build` y finalmente llaman a `electron-builder` para la plataforma correspondiente.

En modo desarrollo, `npm run dev` abre la aplicación y también DevTools para depurar errores del renderer.

Repositorio oficial: https://github.com/YahirHub/piper-neo


### Proveedores LLM externos y CORS

El Live Chat usa un proxy seguro en el proceso principal de Electron para llamar a proveedores OpenAI-compatible `/v1`. Esto evita bloqueos CORS del renderer cuando el proveedor no permite `http://127.0.0.1:5173` durante desarrollo o `file://` en producción.

Configura la URL base, no el endpoint completo:

```txt
https://tu-proveedor/v1
```

No uses:

```txt
https://tu-proveedor/v1/models
https://tu-proveedor/v1/chat/completions
```
