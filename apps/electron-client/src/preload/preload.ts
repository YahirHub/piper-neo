import { contextBridge } from 'electron';

contextBridge.exposeInMainWorld('piperNeoDesktop', {
  appName: 'Piper Neo Client',
  platform: process.platform,
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node
  }
});
