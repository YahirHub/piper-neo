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
      httpRequest?: (request: {
        url: string;
        method?: string;
        headers?: Record<string, string>;
        body?: string;
        timeoutMs?: number;
      }) => Promise<{
        ok: boolean;
        status: number;
        statusText: string;
        headers: Record<string, string>;
        body: string;
      }>;
      clipboardWriteText?: (text: string) => Promise<boolean>;
      httpStream?: (
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
          result?: {
            ok: boolean;
            status: number;
            statusText: string;
            headers: Record<string, string>;
            body: string;
          };
          ok?: boolean;
          status?: number;
          statusText?: string;
          headers?: Record<string, string>;
        }) => void
      ) => Promise<{
        ok: boolean;
        status: number;
        statusText: string;
        headers: Record<string, string>;
        body: string;
      }>;
    };
  }
}

export {};
