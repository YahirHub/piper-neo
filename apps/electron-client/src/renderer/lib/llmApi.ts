import type { LlmChatMessage, LlmModelListResponse, LlmModelInfo } from './types';

export class LlmApiError extends Error {
  code: string;
  status?: number;

  constructor(message: string, code = 'llm_request_failed', status?: number) {
    super(message);
    this.name = 'LlmApiError';
    this.code = code;
    this.status = status;
  }
}

export interface LlmClientOptions {
  apiUrl: string;
  token?: string;
  useToken?: boolean;
}

function normalizeLlmApiUrl(apiUrl: string): string {
  const raw = apiUrl.trim() || 'http://127.0.0.1:11434/v1';
  const withProtocol = /^https?:\/\//i.test(raw) ? raw : `http://${raw}`;
  const url = new URL(withProtocol);

  let pathname = url.pathname.replace(/\/+$/, '');
  pathname = pathname.replace(/\/(chat\/completions|completions|models)$/i, '');

  if (!pathname || pathname === '/') {
    pathname = '/v1';
  } else if (!/(^|\/)v1(\/|$)/i.test(pathname)) {
    pathname = `${pathname}/v1`;
  }

  url.pathname = pathname.replace(/\/{2,}/g, '/');
  url.search = '';
  url.hash = '';
  return url.toString().replace(/\/+$/, '');
}

async function desktopFetch(url: string, init: RequestInit | undefined, timeoutMs: number) {
  const desktopRequest = window.piperNeoDesktop?.httpRequest;
  if (!desktopRequest) return null;

  const headers: Record<string, string> = {};
  const inputHeaders = init?.headers;
  if (inputHeaders instanceof Headers) {
    inputHeaders.forEach((value, key) => {
      headers[key] = value;
    });
  } else if (Array.isArray(inputHeaders)) {
    for (const [key, value] of inputHeaders) headers[key] = value;
  } else if (inputHeaders) {
    Object.assign(headers, inputHeaders as Record<string, string>);
  }

  return await desktopRequest({
    url,
    method: init?.method ?? 'GET',
    headers,
    body: typeof init?.body === 'string' ? init.body : undefined,
    timeoutMs
  });
}

function headersToRecord(inputHeaders: HeadersInit | undefined): Record<string, string> {
  const headers: Record<string, string> = {};
  if (inputHeaders instanceof Headers) {
    inputHeaders.forEach((value, key) => {
      headers[key] = value;
    });
  } else if (Array.isArray(inputHeaders)) {
    for (const [key, value] of inputHeaders) headers[key] = value;
  } else if (inputHeaders) {
    Object.assign(headers, inputHeaders as Record<string, string>);
  }
  return headers;
}

function extractDeltaFromPayload(payload: any): string {
  const delta = payload?.choices?.[0]?.delta?.content;
  if (typeof delta === 'string') return delta;

  const content = payload?.choices?.[0]?.message?.content;
  if (typeof content === 'string') return content;

  const text = payload?.choices?.[0]?.text ?? payload?.response ?? payload?.content;
  return typeof text === 'string' ? text : '';
}

function consumeOpenAiStreamChunk(buffer: string, onDelta: (delta: string) => void): string {
  let remaining = buffer;

  while (true) {
    const eventEnd = remaining.indexOf('\n\n');
    if (eventEnd === -1) break;

    const rawEvent = remaining.slice(0, eventEnd);
    remaining = remaining.slice(eventEnd + 2);

    for (const rawLine of rawEvent.split('\n')) {
      const line = rawLine.trim();
      if (!line || line.startsWith(':')) continue;
      const data = line.startsWith('data:') ? line.slice(5).trim() : line;
      if (!data || data === '[DONE]') continue;

      try {
        const payload = JSON.parse(data);
        const delta = extractDeltaFromPayload(payload);
        if (delta) onDelta(delta);
      } catch {
        // Some local servers stream plain text. Keep it useful instead of failing.
        if (!data.startsWith('{')) onDelta(data);
      }
    }
  }

  return remaining;
}


const LLM_RETRY_DELAYS_MS = [650, 1500];

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function isRetriableLlmError(error: unknown): boolean {
  if (error instanceof LlmApiError) {
    if (error.status && [401, 403, 404].includes(error.status)) return false;
    return ['llm_timeout', 'llm_network_error', 'llm_empty_response', 'llm_request_failed'].includes(error.code)
      || Boolean(error.status && (error.status === 408 || error.status === 409 || error.status === 429 || error.status >= 500));
  }

  return true;
}

async function withRetry<T>(operation: () => Promise<T>, attempts = 3): Promise<T> {
  let lastError: unknown;

  for (let attempt = 0; attempt < attempts; attempt += 1) {
    try {
      return await operation();
    } catch (error) {
      lastError = error;
      if (attempt >= attempts - 1 || !isRetriableLlmError(error)) break;
      await sleep(LLM_RETRY_DELAYS_MS[Math.min(attempt, LLM_RETRY_DELAYS_MS.length - 1)]);
    }
  }

  throw lastError;
}

export class OpenAiCompatibleClient {
  readonly baseUrl: string;
  private readonly token: string;
  private readonly useToken: boolean;

  constructor(options: LlmClientOptions) {
    this.baseUrl = normalizeLlmApiUrl(options.apiUrl);
    this.token = options.token?.trim() ?? '';
    this.useToken = Boolean(options.useToken && this.token);
  }

  private headers(extra?: HeadersInit): HeadersInit {
    const headers: Record<string, string> = {
      Accept: 'application/json',
      ...(extra as Record<string, string> | undefined)
    };

    if (this.useToken) {
      headers.Authorization = `Bearer ${this.token}`;
    }

    return headers;
  }

  private friendlyStatusMessage(status: number): string {
    if (status === 401 || status === 403) {
      return this.useToken
        ? 'El proveedor LLM rechazó el token configurado.'
        : 'El proveedor LLM requiere token/API key.';
    }
    if (status === 404) return 'Endpoint LLM no encontrado. Revisa que la URL base termine en /v1 o sea compatible con OpenAI.';
    if (status >= 500) return 'El proveedor LLM respondió con error interno.';
    return 'El proveedor LLM no pudo completar la solicitud.';
  }

  private async request<T>(path: string, init?: RequestInit, timeoutMs = 45000): Promise<T> {
    const url = `${this.baseUrl}${path.startsWith('/') ? path : `/${path}`}`;
    const requestInit: RequestInit = {
      ...init,
      headers: this.headers(init?.headers)
    };

    try {
      const proxied = await desktopFetch(url, requestInit, timeoutMs);
      if (proxied) {
        const contentType = proxied.headers['content-type'] ?? '';
        const maybeJson = proxied.body.trim().startsWith('{') || proxied.body.trim().startsWith('[');
        const payload = proxied.body && (contentType.includes('application/json') || maybeJson)
          ? JSON.parse(proxied.body)
          : undefined;

        if (!proxied.ok) {
          const message = payload?.error?.message
            ?? payload?.message
            ?? this.friendlyStatusMessage(proxied.status);
          const code = payload?.error?.code ?? (proxied.status === 401 ? 'invalid_llm_token' : 'llm_request_failed');
          throw new LlmApiError(message, code, proxied.status);
        }

        return payload as T;
      }

      const controller = new AbortController();
      const timeout = window.setTimeout(() => controller.abort(), timeoutMs);

      try {
        const response = await fetch(url, {
          ...requestInit,
          signal: controller.signal
        });

        const contentType = response.headers.get('content-type') ?? '';
        const payload = contentType.includes('application/json') ? await response.json() : undefined;

        if (!response.ok) {
          const message = payload?.error?.message
            ?? payload?.message
            ?? this.friendlyStatusMessage(response.status);
          const code = payload?.error?.code ?? (response.status === 401 ? 'invalid_llm_token' : 'llm_request_failed');
          throw new LlmApiError(message, code, response.status);
        }

        return payload as T;
      } finally {
        window.clearTimeout(timeout);
      }
    } catch (error) {
      if (error instanceof LlmApiError) throw error;
      if (error instanceof DOMException && error.name === 'AbortError') {
        throw new LlmApiError('El proveedor LLM no respondió a tiempo.', 'llm_timeout');
      }
      throw new LlmApiError('No se pudo conectar con el proveedor LLM. Revisa la URL base /v1, el token y que el servidor esté activo.', 'llm_network_error');
    }
  }

  async models(): Promise<LlmModelInfo[]> {
    const payload = await withRetry(() => this.request<LlmModelListResponse>('/models', undefined, 15000));
    return Array.isArray(payload?.data) ? payload.data : [];
  }

  async chatCompletion(input: {
    model: string;
    messages: LlmChatMessage[];
    temperature?: number;
    maxTokens?: number;
  }): Promise<string> {
    let output = '';
    await this.chatCompletionStream({
      ...input,
      onDelta: (delta) => {
        output += delta;
      }
    });
    return output.trim();
  }

  async chatCompletionStream(input: {
    model: string;
    messages: LlmChatMessage[];
    temperature?: number;
    maxTokens?: number;
    onDelta: (delta: string) => void;
  }): Promise<string> {
    let lastError: unknown;

    for (let attempt = 0; attempt < 3; attempt += 1) {
      let streamedSomething = false;
      try {
        return await this.chatCompletionStreamOnce({
          ...input,
          onDelta: (delta) => {
            streamedSomething = true;
            input.onDelta(delta);
          }
        });
      } catch (error) {
        lastError = error;
        // If the provider already streamed partial text, do not retry because
        // the UI would duplicate text from the second attempt.
        if (streamedSomething || attempt >= 2 || !isRetriableLlmError(error)) break;
        await sleep(LLM_RETRY_DELAYS_MS[Math.min(attempt, LLM_RETRY_DELAYS_MS.length - 1)]);
      }
    }

    throw lastError;
  }

  private async chatCompletionStreamOnce(input: {
    model: string;
    messages: LlmChatMessage[];
    temperature?: number;
    maxTokens?: number;
    onDelta: (delta: string) => void;
  }): Promise<string> {
    const body: Record<string, unknown> = {
      model: input.model,
      messages: input.messages,
      stream: true,
      temperature: input.temperature ?? 0.7
    };

    if (input.maxTokens && input.maxTokens > 0) {
      body.max_tokens = input.maxTokens;
    }

    const url = `${this.baseUrl}/chat/completions`;
    const requestInit: RequestInit = {
      method: 'POST',
      headers: this.headers({ 'Content-Type': 'application/json', Accept: 'text/event-stream, application/json' }),
      body: JSON.stringify(body)
    };

    let fullText = '';
    const appendDelta = (delta: string) => {
      fullText += delta;
      input.onDelta(delta);
    };

    try {
      const desktopStream = window.piperNeoDesktop?.httpStream;
      if (desktopStream) {
        let responseStatus = 200;
        let responseOk = true;
        let responseText = '';
        let buffer = '';

        const final = await desktopStream(
          {
            url,
            method: 'POST',
            headers: headersToRecord(requestInit.headers),
            body: typeof requestInit.body === 'string' ? requestInit.body : undefined,
            timeoutMs: 180000
          },
          (event) => {
            if (event.type === 'response') {
              responseStatus = event.status ?? 0;
              responseOk = Boolean(event.ok);
              return;
            }
            if (event.type === 'chunk' && event.chunk) {
              responseText += event.chunk;
              buffer = consumeOpenAiStreamChunk(buffer + event.chunk.replace(/\r\n/g, '\n'), appendDelta);
            }
          }
        );

        if (!responseOk || !final.ok) {
          const bodyText = final.body || responseText;
          let message = this.friendlyStatusMessage(responseStatus || final.status);
          try {
            const payload = JSON.parse(bodyText);
            message = payload?.error?.message ?? payload?.message ?? message;
          } catch {}
          throw new LlmApiError(message, responseStatus === 401 ? 'invalid_llm_token' : 'llm_request_failed', responseStatus || final.status);
        }

        if (!fullText && responseText.trim()) {
          try {
            const payload = JSON.parse(responseText);
            const fallback = payload?.choices?.[0]?.message?.content ?? payload?.choices?.[0]?.text ?? payload?.content;
            if (typeof fallback === 'string') appendDelta(fallback);
          } catch {}
        }

        if (!fullText.trim()) {
          throw new LlmApiError('El proveedor LLM respondió, pero no devolvió contenido de chat.', 'llm_empty_response');
        }
        return fullText.trim();
      }

      const response = await fetch(url, requestInit);
      if (!response.ok) {
        let message = this.friendlyStatusMessage(response.status);
        try {
          const payload = await response.json();
          message = payload?.error?.message ?? payload?.message ?? message;
        } catch {}
        throw new LlmApiError(message, response.status === 401 ? 'invalid_llm_token' : 'llm_request_failed', response.status);
      }

      const reader = response.body?.getReader();
      if (!reader) {
        const payload = await response.json();
        const fallback = payload?.choices?.[0]?.message?.content ?? payload?.choices?.[0]?.text ?? payload?.content;
        if (typeof fallback === 'string') appendDelta(fallback);
        return fullText.trim();
      }

      const decoder = new TextDecoder();
      let buffer = '';
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer = consumeOpenAiStreamChunk(buffer + decoder.decode(value, { stream: true }).replace(/\r\n/g, '\n'), appendDelta);
      }
      buffer = consumeOpenAiStreamChunk(buffer + decoder.decode(), appendDelta);

      if (!fullText.trim()) {
        throw new LlmApiError('El proveedor LLM respondió, pero no devolvió contenido de chat.', 'llm_empty_response');
      }
      return fullText.trim();
    } catch (error) {
      if (error instanceof LlmApiError) throw error;
      throw new LlmApiError('No se pudo conectar con el proveedor LLM. Revisa la URL base /v1, el token y que el servidor esté activo.', 'llm_network_error');
    }
  }
}
