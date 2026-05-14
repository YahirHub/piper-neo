# Instrucciones para continuar en una nueva conversación

## Base recomendada

Para continuar modificando Electron, usar como base:

```txt
/mnt/data/piper-neo-electron-ui-polish-fix.zip
```

Si el usuario sube un ZIP nuevo de su repo, usar ese como fuente de verdad.

## Regla para entregar ZIPs

- Excluir `.git`.
- Excluir `node_modules`.
- Excluir salidas de build:
  - `dist`
  - `release`
  - `out`
  - `.vite`

Ejemplo:

```bash
cd /mnt/data/workdir
zip -qr /mnt/data/nuevo.zip piper-neo \
  -x 'piper-neo/.git/*' \
     'piper-neo/apps/electron-client/node_modules/*' \
     'piper-neo/apps/electron-client/dist/*' \
     'piper-neo/apps/electron-client/release/*' \
     'piper-neo/apps/electron-client/out/*'
```

## Validación TSX rápida

```bash
cd /mnt/data/workdir/piper-neo/apps/electron-client
node - <<'NODE'
const ts = require('typescript');
const fs = require('fs');
const files = [
  'src/renderer/components/ChatPage.tsx',
  'src/renderer/components/StudioPage.tsx',
  'src/renderer/components/Onboarding.tsx',
  'src/renderer/App.tsx'
].filter(fs.existsSync);
let failed = false;
for (const file of files) {
  const source = fs.readFileSync(file, 'utf8');
  const result = ts.transpileModule(source, {
    compilerOptions: {
      jsx: ts.JsxEmit.ReactJSX,
      target: ts.ScriptTarget.ES2020,
      module: ts.ModuleKind.ESNext
    },
    reportDiagnostics: true,
    fileName: file
  });
  const errors = (result.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error);
  for (const d of errors) {
    console.error(file + ': ' + ts.flattenDiagnosticMessageText(d.messageText, '\n'));
    failed = true;
  }
}
process.exit(failed ? 1 : 0);
NODE
```

## Cosas a evitar

- No prometer commits automáticos.
- No incluir `.git` en el ZIP final salvo que el usuario lo pida.
- No usar CDN para iconos/librerías si se puede evitar.
- No reescribir texto del usuario con LLM para TTS por defecto.
- No meter cambios grandes de core Piper si el usuario solo pide app Electron.
