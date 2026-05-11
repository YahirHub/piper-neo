import { app, BrowserWindow, clipboard, ipcMain, shell, session } from 'electron';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const isDev = Boolean(process.env.VITE_DEV_SERVER_URL);


interface DesktopHttpRequest {
  url: string;
  method?: string;
  headers?: Record<string, string>;
  body?: string;
  timeoutMs?: number;
}

interface DesktopHttpResponse {
  ok: boolean;
  status: number;
  statusText: string;
  headers: Record<string, string>;
  body: string;
}

interface DesktopHttpStreamRequest extends DesktopHttpRequest {
  streamId: string;
}

function registerClipboardBridge(): void {
  ipcMain.handle('desktop:clipboard-write-text', async (_event, text: string): Promise<boolean> => {
    clipboard.writeText(String(text ?? ''));
    return true;
  });
}

function registerHttpProxy(): void {
  ipcMain.handle('desktop:http-request', async (_event, input: DesktopHttpRequest): Promise<DesktopHttpResponse> => {
    const target = new URL(input.url);
    if (target.protocol !== 'http:' && target.protocol !== 'https:') {
      throw new Error('Solo se permiten solicitudes HTTP/HTTPS desde el cliente.');
    }

    const method = (input.method ?? 'GET').toUpperCase();
    if (!['GET', 'POST'].includes(method)) {
      throw new Error(`Método HTTP no permitido: ${method}`);
    }

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), Math.max(1000, input.timeoutMs ?? 45000));

    try {
      const response = await fetch(target, {
        method,
        headers: input.headers ?? {},
        body: method === 'POST' ? input.body : undefined,
        signal: controller.signal
      });

      const headers: Record<string, string> = {};
      response.headers.forEach((value, key) => {
        headers[key.toLowerCase()] = value;
      });

      return {
        ok: response.ok,
        status: response.status,
        statusText: response.statusText,
        headers,
        body: await response.text()
      };
    } finally {
      clearTimeout(timeout);
    }
  });

  ipcMain.handle('desktop:http-stream', async (event, input: DesktopHttpStreamRequest): Promise<DesktopHttpResponse> => {
    const target = new URL(input.url);
    if (target.protocol !== 'http:' && target.protocol !== 'https:') {
      throw new Error('Solo se permiten solicitudes HTTP/HTTPS desde el cliente.');
    }

    const method = (input.method ?? 'POST').toUpperCase();
    if (!['GET', 'POST'].includes(method)) {
      throw new Error(`Método HTTP no permitido: ${method}`);
    }

    const channel = `desktop:http-stream:${input.streamId}`;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), Math.max(1000, input.timeoutMs ?? 180000));
    const decoder = new TextDecoder();

    try {
      const response = await fetch(target, {
        method,
        headers: input.headers ?? {},
        body: method === 'POST' ? input.body : undefined,
        signal: controller.signal
      });

      const headers: Record<string, string> = {};
      response.headers.forEach((value, key) => {
        headers[key.toLowerCase()] = value;
      });

      event.sender.send(channel, {
        type: 'response',
        ok: response.ok,
        status: response.status,
        statusText: response.statusText,
        headers
      });

      let body = '';
      if (response.body) {
        const reader = response.body.getReader();
        while (true) {
          const { done, value } = await reader.read();
          if (done) break;
          const chunk = decoder.decode(value, { stream: true });
          if (chunk) {
            body += chunk;
            event.sender.send(channel, { type: 'chunk', chunk });
          }
        }
        const tail = decoder.decode();
        if (tail) {
          body += tail;
          event.sender.send(channel, { type: 'chunk', chunk: tail });
        }
      } else {
        body = await response.text();
        if (body) event.sender.send(channel, { type: 'chunk', chunk: body });
      }

      const result = { ok: response.ok, status: response.status, statusText: response.statusText, headers, body };
      event.sender.send(channel, { type: 'done', result });
      return result;
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Streaming HTTP request failed';
      event.sender.send(channel, { type: 'error', message });
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  });
}

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1240,
    height: 820,
    minWidth: 960,
    minHeight: 640,
    show: false,
    backgroundColor: '#070b16',
    autoHideMenuBar: true,
    title: 'Piper Neo Client',
    webPreferences: {
      preload: path.join(__dirname, '../preload/preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      webSecurity: true
    }
  });

  win.once('ready-to-show', () => win.show());

  win.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('https://') || url.startsWith('http://')) {
      void shell.openExternal(url);
    }
    return { action: 'deny' };
  });

  win.webContents.on('will-navigate', (event, url) => {
    if (isDev && url.startsWith(process.env.VITE_DEV_SERVER_URL ?? '')) {
      return;
    }

    if (!url.startsWith('file://')) {
      event.preventDefault();
      if (url.startsWith('https://') || url.startsWith('http://')) {
        void shell.openExternal(url);
      }
    }
  });

  win.webContents.on('did-fail-load', (_event, errorCode, errorDescription, validatedURL) => {
    console.error(`[Piper Neo Client] renderer load failed (${errorCode}): ${errorDescription} - ${validatedURL}`);
  });

  win.webContents.on('render-process-gone', (_event, details) => {
    console.error(`[Piper Neo Client] renderer process gone: ${details.reason}`);
  });

  if (isDev) {
    void win.loadURL(process.env.VITE_DEV_SERVER_URL as string);
    win.webContents.once('did-finish-load', () => {
      win.webContents.openDevTools({ mode: 'detach' });
    });
  } else {
    void win.loadFile(path.join(__dirname, '../renderer/index.html'));
  }
}

app.whenReady().then(() => {
  registerHttpProxy();
  registerClipboardBridge();

  session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => {
    callback(false);
  });

  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
