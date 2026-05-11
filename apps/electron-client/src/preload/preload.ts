import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('piperNeoDesktop', {
  appName: 'Piper Neo Client',
  platform: process.platform,
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node
  },
  httpRequest: (request: {
    url: string;
    method?: string;
    headers?: Record<string, string>;
    body?: string;
    timeoutMs?: number;
  }) => ipcRenderer.invoke('desktop:http-request', request),
  clipboardWriteText: (text: string) => ipcRenderer.invoke('desktop:clipboard-write-text', text),
  httpStream: (
    request: {
      url: string;
      method?: string;
      headers?: Record<string, string>;
      body?: string;
      timeoutMs?: number;
    },
    onEvent: (event: {
      type: 'response' | 'chunk' | 'done' | 'error';
      chunk?: string;
      message?: string;
      result?: unknown;
      ok?: boolean;
      status?: number;
      statusText?: string;
      headers?: Record<string, string>;
    }) => void
  ) => {
    const streamId = `stream-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const channel = `desktop:http-stream:${streamId}`;
    const listener = (_event: Electron.IpcRendererEvent, payload: any) => onEvent(payload);
    ipcRenderer.on(channel, listener);
    return ipcRenderer.invoke('desktop:http-stream', { ...request, streamId }).finally(() => {
      ipcRenderer.removeListener(channel, listener);
    });
  }
});
