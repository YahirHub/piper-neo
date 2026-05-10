/// <reference types="vite/client" />

declare global {
  interface Window {
    piperNeoDesktop?: {
      appName: string;
      platform: string;
      versions: {
        electron: string;
        chrome: string;
        node: string;
      };
    };
  }
}

export {};
