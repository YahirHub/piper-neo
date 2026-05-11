import type { AppSettings, ChatMessage, RoutePath } from './types';

const SETTINGS_KEY = 'piper-neo-client:settings:v1';
const DRAFT_KEY = 'piper-neo-client:draft:v1';
const CHAT_MESSAGES_KEY = 'piper-neo-client:chat-messages:v1';
const CHAT_DRAFT_KEY = 'piper-neo-client:chat-draft:v1';

export const defaultSettings: AppSettings = {
  apiUrl: 'http://127.0.0.1:8080',
  token: '',
  useToken: false,
  connected: false,
  selectedModel: '',
  speakerId: 0,
  autoPlay: true,
  saveHistory: true,
  lastRoute: '/setup',
  modelReturnRoute: '',
  llmApiUrl: 'http://127.0.0.1:11434/v1',
  llmToken: '',
  llmUseToken: false,
  llmModel: '',
  llmManualModel: '',
  llmSystemPrompt: 'Eres un asistente útil, claro y conversacional. Responde en español de forma natural para que el texto pueda convertirse a voz.',
  llmTemperature: 0.7,
  llmMaxTokens: 700,
  llmContextMessages: 12
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
  localStorage.removeItem(CHAT_MESSAGES_KEY);
  localStorage.removeItem(CHAT_DRAFT_KEY);
  return defaultSettings;
}

export function loadDraft(): string {
  return localStorage.getItem(DRAFT_KEY) ?? '';
}

export function saveDraft(text: string): void {
  localStorage.setItem(DRAFT_KEY, text);
}

export function isRoutePath(value: string): value is RoutePath {
  return ['/', '/setup', '/models', '/chat', '/history', '/settings'].includes(value);
}

export function loadChatMessages(): ChatMessage[] {
  try {
    const raw = localStorage.getItem(CHAT_MESSAGES_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveChatMessages(messages: ChatMessage[]): void {
  localStorage.setItem(CHAT_MESSAGES_KEY, JSON.stringify(messages));
}

export function clearChatMessages(): void {
  localStorage.removeItem(CHAT_MESSAGES_KEY);
}

export function loadChatDraft(): string {
  return localStorage.getItem(CHAT_DRAFT_KEY) ?? '';
}

export function saveChatDraft(text: string): void {
  localStorage.setItem(CHAT_DRAFT_KEY, text);
}
