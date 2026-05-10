import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';

export default defineConfig({
  // Required for packaged Electron builds loaded with file://.
  // Without this, Vite emits /assets/... and the installed app tries
  // to load file:///assets/..., which leaves the window black.
  base: './',
  root: '.',
  plugins: [react()],
  build: {
    outDir: 'dist/renderer',
    emptyOutDir: false,
    rollupOptions: {
      input: resolve(__dirname, 'index.html')
    }
  },
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true
  }
});
