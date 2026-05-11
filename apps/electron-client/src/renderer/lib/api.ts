import type { ModelsPayload, PiperResponse, PiperStatus, TtsPayload } from './types';

export class PiperApiError extends Error {
  code: string;
  status?: number;

  constructor(message: string, code = 'request_failed', status?: number) {
    super(message);
    this.name = 'PiperApiError';
    this.code = code;
    this.status = status;
  }
}


const RETRY_DELAYS_MS = [420, 1100];

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function isRetriablePiperError(error: unknown): boolean {
  if (error instanceof PiperApiError) {
    if (error.status && [401, 403, 404].includes(error.status)) return false;
    return ['timeout', 'network_error', 'audio_download_failed', 'image_failed', 'request_failed'].includes(error.code)
      || Boolean(error.status && (error.status === 408 || error.status === 429 || error.status >= 500));
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
      if (attempt >= attempts - 1 || !isRetriablePiperError(error)) break;
      await sleep(RETRY_DELAYS_MS[Math.min(attempt, RETRY_DELAYS_MS.length - 1)]);
    }
  }

  throw lastError;
}

export interface PiperClientOptions {
  apiUrl: string;
  token?: string;
  useToken?: boolean;
}

function normalizeApiUrl(apiUrl: string): string {
  const value = apiUrl.trim().replace(/\/+$/, '');
  if (!value) return 'http://127.0.0.1:8080';
  if (!/^https?:\/\//i.test(value)) return `http://${value}`.replace(/\/+$/, '');
  return value;
}

export class PiperApiClient {
  readonly baseUrl: string;
  private readonly token: string;
  private readonly useToken: boolean;

  constructor(options: PiperClientOptions) {
    this.baseUrl = normalizeApiUrl(options.apiUrl);
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

  private async request<T>(path: string, init?: RequestInit, timeoutMs = 10000): Promise<T> {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), timeoutMs);
    const url = `${this.baseUrl}${path.startsWith('/') ? path : `/${path}`}`;

    try {
      const response = await fetch(url, {
        ...init,
        headers: this.headers(init?.headers),
        signal: controller.signal
      });

      const contentType = response.headers.get('content-type') ?? '';
      const payload = contentType.includes('application/json')
        ? ((await response.json()) as PiperResponse<T>)
        : undefined;

      if (!response.ok || payload?.success === false) {
        const code = payload?.error ?? (response.status === 401 ? 'invalid_token' : 'request_failed');
        const rawMessage = payload?.message ?? this.friendlyStatusMessage(response.status, code);
        const message = rawMessage.includes('Invalid Feed Input Name:sid')
          ? 'El modelo seleccionado parece ser de una sola voz y rechazó speaker_id. Vuelve a intentar; el cliente ya evitará enviar speaker_id cuando la metadata indique un solo speaker.'
          : rawMessage;
        throw new PiperApiError(message, code, response.status);
      }

      return (payload?.data ?? payload) as T;
    } catch (error) {
      if (error instanceof PiperApiError) throw error;
      if (error instanceof DOMException && error.name === 'AbortError') {
        throw new PiperApiError('El servidor no respondió a tiempo. Revisa que Piper Neo esté iniciado.', 'timeout');
      }
      throw new PiperApiError('No se pudo conectar con Piper Neo. Verifica la URL y que el servidor local esté activo.', 'network_error');
    } finally {
      window.clearTimeout(timeout);
    }
  }

  private friendlyStatusMessage(status: number, code: string): string {
    if (status === 401 || code === 'invalid_token') {
      return this.useToken
        ? 'La API rechazó el token. Revisa la key configurada.'
        : 'Este servidor requiere token. Activa la opción de API key e inténtalo de nuevo.';
    }
    if (status === 404) return 'Endpoint no encontrado. Revisa que sea un servidor Piper Neo compatible.';
    if (status >= 500) return 'Piper Neo respondió con error interno.';
    return 'La solicitud no pudo completarse.';
  }

  async check(): Promise<PiperStatus> {
    try {
      return await this.request<PiperStatus>('/api/v1/status', undefined, 7000);
    } catch (error) {
      if (error instanceof PiperApiError && error.status === 404) {
        return await this.request<PiperStatus>('/api/health', undefined, 7000);
      }
      throw error;
    }
  }

  async models(): Promise<ModelsPayload> {
    return await withRetry(() => this.request<ModelsPayload>('/api/v1/models?include=metadata', undefined, 15000));
  }

  async modelImage(imageUrl: string): Promise<Blob> {
    return await withRetry(async () => {
      const url = imageUrl.startsWith('http') ? imageUrl : `${this.baseUrl}${imageUrl}`;
      const response = await fetch(url, {
        headers: this.headers({ Accept: 'image/*' })
      });

      if (!response.ok) {
        if (response.status === 401) {
          throw new PiperApiError('La API rechazó el token al cargar la imagen.', 'invalid_token', 401);
        }
        throw new PiperApiError('No se pudo cargar la imagen del modelo.', 'image_failed', response.status);
      }

      return await response.blob();
    });
  }

  async synthesize(input: { text: string; model: string; speakerId?: number }): Promise<TtsPayload> {
    const body: Record<string, string | number> = {
      text: input.text,
      model: input.model
    };

    // Piper single-speaker ONNX models usually do not expose the `sid` input.
    // Sending speaker_id=0 to those models makes ONNX Runtime fail with:
    // "Invalid Feed Input Name:sid". Only send speaker_id when the selected
    // voice is actually multi-speaker.
    if (typeof input.speakerId === 'number' && Number.isFinite(input.speakerId)) {
      body.speaker_id = input.speakerId;
    }

    return await withRetry(() => this.request<TtsPayload>(
      '/api/v1/tts',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      },
      120000
    ));
  }

  async audio(audioUrl: string): Promise<Blob> {
    return await withRetry(async () => {
      const url = audioUrl.startsWith('http') ? audioUrl : `${this.baseUrl}${audioUrl}`;
      const response = await fetch(url, {
        headers: this.headers({ Accept: 'audio/wav,audio/*' })
      });

      if (!response.ok) {
        if (response.status === 401) {
          throw new PiperApiError('La API rechazó el token al descargar el audio.', 'invalid_token', 401);
        }
        throw new PiperApiError('El audio fue generado, pero no se pudo descargar.', 'audio_download_failed', response.status);
      }

      return await response.blob();
    });
  }
}
