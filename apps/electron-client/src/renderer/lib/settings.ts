import type { AppSettings, RoutePath } from './types';

const SETTINGS_KEY = 'piper-neo-client:settings:v1';
const DRAFT_KEY = 'piper-neo-client:draft:v1';

export const defaultSettings: AppSettings = {
  apiUrl: 'http://127.0.0.1:8080',
  token: '',
  useToken: false,
  connected: false,
  selectedModel: '',
  speakerId: 0,
  autoPlay: true,
  saveHistory: true,
  lastRoute: '/setup'
};

export function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (!raw) return defaultSettings;
    return { ...defaultSettings, ...JSON.parse(raw) };
  } catch {
    return defaultSettings;
  }
}

export function saveSettings(settings: AppSettings): void {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
}

export function resetSettings(): AppSettings {
  localStorage.removeItem(SETTINGS_KEY);
  localStorage.removeItem(DRAFT_KEY);
  return defaultSettings;
}

export function loadDraft(): string {
  return localStorage.getItem(DRAFT_KEY) ?? '';
}

export function saveDraft(text: string): void {
  localStorage.setItem(DRAFT_KEY, text);
}

export function isRoutePath(value: string): value is RoutePath {
  return ['/', '/setup', '/models', '/history', '/settings'].includes(value);
}
