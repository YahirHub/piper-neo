import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { PiperApiClient, PiperApiError } from '../lib/api';
import { OpenAiCompatibleClient, LlmApiError } from '../lib/llmApi';
import {
  clearChatMessages,
  loadChatDraft,
  loadChatMessages,
  saveChatDraft,
  saveChatMessages
} from '../lib/settings';
import type {
  AppSettings,
  ChatMessage,
  LlmChatMessage,
  LlmModelInfo,
  PiperModel,
  RoutePath
} from '../lib/types';
import { Icon } from '../lib/icons';
import { formatBytes, formatSeconds, modelDisplayName } from '../lib/format';
import { StatusPill } from './StatusPill';

interface ChatPageProps {
  settings: AppSettings;
  updateSettings: (next: Partial<AppSettings>) => void;
  navigate: (path: RoutePath) => void;
  chatAudioBlobs: Record<string, Blob>;
  setChatAudioBlobs: (next: Record<string, Blob>) => void;
}

function makeId(prefix: string) {
  return `${prefix}-${Date.now()}-${crypto.randomUUID()}`;
}

function clampNumber(value: number, fallback: number, min: number, max: number) {
  if (!Number.isFinite(value)) return fallback;
  return Math.max(min, Math.min(max, value));
}

function activeLlmModel(settings: AppSettings) {
  return (settings.llmModel || settings.llmManualModel).trim();
}

function buildOpenAiMessages(messages: ChatMessage[], settings: AppSettings): LlmChatMessage[] {
  const maxContext = clampNumber(settings.llmContextMessages, 12, 2, 40);
  const context = messages
    .filter((message) => !message.error)
    .slice(-maxContext)
    .map((message) => ({
      role: message.role,
      content: message.content
    } as LlmChatMessage));

  const systemPrompt = settings.llmSystemPrompt.trim();
  return systemPrompt ? [{ role: 'system', content: systemPrompt }, ...context] : context;
}

function stripMarkdownForSpeech(markdown: string): string {
  // El texto que se manda a Piper debe ser plano, pero no debe perder el
  // contenido de bloques/etiquetas de código: algunos LLM ponen palabras,
  // comandos cortos o nombres importantes dentro de `code`.
  const plain = markdown
    .replace(/```[^\n`]*\n?([\s\S]*?)```/g, '\n$1\n')
    .replace(/~~~[^\n~]*\n?([\s\S]*?)~~~/g, '\n$1\n')
    .replace(/`([^`]+)`/g, '$1')
    .replace(/!\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
    .replace(/<br\s*\/?\s*>/gi, '\n')
    .replace(/<[^>]+>/g, ' ')
    .replace(/^\s{0,3}#{1,6}\s+/gm, '')
    .replace(/^\s*[-*+]\s+/gm, '')
    .replace(/^\s*\d+[.)]\s+/gm, '')
    .replace(/^\s*>\s?/gm, '')
    .replace(/[\*_~#]/g, '')
    .replace(/[|]{2,}/g, ' ');

  return plain
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
    .join('. ')
    .replace(/\s+/g, ' ')
    .trim();
}

function splitMarkdown(content: string) {
  const parts: Array<{ type: 'code'; lang: string; text: string } | { type: 'text'; text: string }> = [];
  const regex = /```([^\n`]*)\n?([\s\S]*?)```/g;
  let lastIndex = 0;
  let match: RegExpExecArray | null;

  while ((match = regex.exec(content)) !== null) {
    if (match.index > lastIndex) {
      parts.push({ type: 'text', text: content.slice(lastIndex, match.index) });
    }
    parts.push({ type: 'code', lang: match[1]?.trim() || 'text', text: match[2] ?? '' });
    lastIndex = regex.lastIndex;
  }

  if (lastIndex < content.length) {
    parts.push({ type: 'text', text: content.slice(lastIndex) });
  }

  return parts.length ? parts : [{ type: 'text' as const, text: content }];
}

function tokenizeInline(text: string): ReactNode[] {
  const nodes: ReactNode[] = [];
  const regex = /(`[^`]+`|\*\*[^*]+\*\*|__[^_]+__|\[[^\]]+\]\([^)]+\))/g;
  let last = 0;
  let match: RegExpExecArray | null;

  while ((match = regex.exec(text)) !== null) {
    if (match.index > last) nodes.push(text.slice(last, match.index));
    const token = match[0];
    if (token.startsWith('`')) {
      nodes.push(<code key={`${match.index}-code`} className="inline-code">{token.slice(1, -1)}</code>);
    } else if (token.startsWith('**') || token.startsWith('__')) {
      nodes.push(<strong key={`${match.index}-strong`}>{token.slice(2, -2)}</strong>);
    } else {
      const link = token.match(/^\[([^\]]+)\]\(([^)]+)\)$/);
      nodes.push(link ? <a key={`${match.index}-link`} href={link[2]} target="_blank" rel="noreferrer">{link[1]}</a> : token);
    }
    last = regex.lastIndex;
  }

  if (last < text.length) nodes.push(text.slice(last));
  return nodes;
}

function highlightCode(code: string) {
  const keywordPattern = /\b(import|from|export|const|let|var|function|return|class|interface|type|if|else|for|while|async|await|try|catch|new|public|private|protected|void|string|number|boolean|null|undefined|true|false)\b/g;
  const chunks: ReactNode[] = [];
  let cursor = 0;
  const regex = /(\/\/.*$|#.*$|"(?:\\.|[^"])*"|'(?:\\.|[^'])*'|`(?:\\.|[^`])*`|\b\d+(?:\.\d+)?\b|\b(?:import|from|export|const|let|var|function|return|class|interface|type|if|else|for|while|async|await|try|catch|new|public|private|protected|void|string|number|boolean|null|undefined|true|false)\b)/gm;
  let match: RegExpExecArray | null;

  while ((match = regex.exec(code)) !== null) {
    if (match.index > cursor) chunks.push(code.slice(cursor, match.index));
    const token = match[0];
    let className = 'tok-plain';
    if (/^(\/\/|#)/.test(token)) className = 'tok-comment';
    else if (/^["'`]/.test(token)) className = 'tok-string';
    else if (/^\d/.test(token)) className = 'tok-number';
    else if (keywordPattern.test(token)) className = 'tok-keyword';
    keywordPattern.lastIndex = 0;
    chunks.push(<span key={`${match.index}-${token}`} className={className}>{token}</span>);
    cursor = regex.lastIndex;
  }

  if (cursor < code.length) chunks.push(code.slice(cursor));
  return chunks;
}

function MarkdownMessage({ content }: { content: string }) {
  const [copied, setCopied] = useState('');
  const parts = splitMarkdown(content);

  async function copyCode(code: string) {
    const text = code.trimEnd();
    try {
      if (window.piperNeoDesktop?.clipboardWriteText) {
        await window.piperNeoDesktop.clipboardWriteText(text);
      } else if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(text);
      } else {
        const textarea = document.createElement('textarea');
        textarea.value = text;
        textarea.setAttribute('readonly', 'true');
        textarea.style.position = 'fixed';
        textarea.style.opacity = '0';
        document.body.appendChild(textarea);
        textarea.select();
        document.execCommand('copy');
        textarea.remove();
      }

      setCopied(partKeyForCopy(text));
      window.setTimeout(() => setCopied(''), 1300);
    } catch {
      setCopied('error');
      window.setTimeout(() => setCopied(''), 1600);
    }
  }

  function partKeyForCopy(code: string) {
    return `${code.length}:${code.slice(0, 80)}`;
  }

  return (
    <div className="markdown-message">
      {parts.map((part, index) => {
        if (part.type === 'code') {
          return (
            <div className="code-card" key={`code-${index}`}>
              <div className="code-card-top">
                <span>{part.lang || 'code'}</span>
                <button className="ghost-icon-button" onClick={() => void copyCode(part.text)} title="Copiar código">
                  {copied === 'error' ? <Icon name="alert" /> : copied === partKeyForCopy(part.text.trimEnd()) ? <Icon name="check" /> : <Icon name="copy" />}
                </button>
              </div>
              <pre><code>{highlightCode(part.text)}</code></pre>
            </div>
          );
        }

        return part.text
          .split(/\n{2,}/)
          .map((paragraph, pIndex) => {
            const trimmed = paragraph.trim();
            if (!trimmed) return null;
            if (/^\s*[-*+]\s+/m.test(trimmed)) {
              const items = trimmed.split('\n').map((line) => line.replace(/^\s*[-*+]\s+/, '').trim()).filter(Boolean);
              return <ul key={`text-${index}-${pIndex}`}>{items.map((item, itemIndex) => <li key={itemIndex}>{tokenizeInline(item)}</li>)}</ul>;
            }
            return <p key={`text-${index}-${pIndex}`}>{tokenizeInline(trimmed)}</p>;
          });
      })}
    </div>
  );
}

export function ChatPage({
  settings,
  updateSettings,
  navigate,
  chatAudioBlobs,
  setChatAudioBlobs
}: ChatPageProps) {
  const piperClient = useMemo(() => new PiperApiClient(settings), [settings.apiUrl, settings.token, settings.useToken]);
  const llmClient = useMemo(
    () => new OpenAiCompatibleClient({ apiUrl: settings.llmApiUrl, token: settings.llmToken, useToken: settings.llmUseToken }),
    [settings.llmApiUrl, settings.llmToken, settings.llmUseToken]
  );

  const [messages, setMessages] = useState<ChatMessage[]>(() => loadChatMessages());
  const [draft, setDraft] = useState(() => loadChatDraft());
  const [llmModels, setLlmModels] = useState<LlmModelInfo[]>([]);
  const [piperModels, setPiperModels] = useState<PiperModel[]>([]);
  const [loadingModels, setLoadingModels] = useState(false);
  const [busy, setBusy] = useState(false);
  const [audioBusyId, setAudioBusyId] = useState('');
  const [error, setError] = useState('');
  const [modelError, setModelError] = useState('');
  const [playbackUrl, setPlaybackUrl] = useState('');
  const [playbackMessageId, setPlaybackMessageId] = useState('');
  const [pendingPlay, setPendingPlay] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const listEndRef = useRef<HTMLDivElement | null>(null);
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const autoScrollEnabledRef = useRef(true);
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const [autoScrollEnabled, setAutoScrollEnabled] = useState(true);

  const selectedPiperModel = piperModels.find((model) => model.file === settings.selectedModel);
  const selectedLlmModel = activeLlmModel(settings);
  const canSend = Boolean(draft.trim() && selectedLlmModel && settings.selectedModel && !busy);

  function setAutoScroll(value: boolean) {
    autoScrollEnabledRef.current = value;
    setAutoScrollEnabled(value);
  }

  function scrollChatToBottom(behavior: ScrollBehavior = 'smooth') {
    const scroll = scrollRef.current;
    if (!scroll) return;
    scroll.scrollTo({ top: scroll.scrollHeight, behavior });
  }

  function enableAutoScroll(behavior: ScrollBehavior = 'smooth') {
    setAutoScroll(true);
    window.requestAnimationFrame(() => scrollChatToBottom(behavior));
  }

  function handleChatScroll() {
    const scroll = scrollRef.current;
    if (!scroll) return;

    const distanceFromBottom = scroll.scrollHeight - scroll.scrollTop - scroll.clientHeight;
    const shouldAutoScroll = distanceFromBottom < 90;
    if (shouldAutoScroll !== autoScrollEnabledRef.current) {
      setAutoScroll(shouldAutoScroll);
    }
  }

  useEffect(() => {
    saveChatMessages(messages);
  }, [messages]);

  useEffect(() => {
    saveChatDraft(draft);
    const textarea = textareaRef.current;
    if (!textarea) return;
    textarea.style.height = 'auto';
    textarea.style.height = `${Math.min(textarea.scrollHeight, 210)}px`;
  }, [draft]);

  useEffect(() => {
    if (!autoScrollEnabledRef.current) return;

    const frame = window.requestAnimationFrame(() => {
      scrollChatToBottom('smooth');
    });

    return () => window.cancelAnimationFrame(frame);
  }, [messages, busy]);

  useEffect(() => {
    void refreshPiperModels(false);
  }, [piperClient]);

  useEffect(() => {
    if (!pendingPlay || !playbackUrl) return;
    setPendingPlay(false);
    window.setTimeout(() => void audioRef.current?.play(), 120);
  }, [pendingPlay, playbackUrl]);

  useEffect(() => {
    setShowSettings(false);
  }, []);

  useEffect(() => {
    if (!showSettings) return;

    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') setShowSettings(false);
    }

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [showSettings]);

  useEffect(() => {
    return () => {
      if (playbackUrl) URL.revokeObjectURL(playbackUrl);
    };
  }, [playbackUrl]);

  async function refreshPiperModels(showErrors = true) {
    setLoadingModels(true);
    if (showErrors) {
      setModelError('');
      setError('');
    }

    try {
      const piperResult = await piperClient.models();
      setPiperModels(piperResult.models ?? []);
    } catch {
      if (showErrors) setModelError('No se pudieron obtener los modelos de Piper Neo.');
    } finally {
      setLoadingModels(false);
    }
  }

  async function refreshAllModels(showErrors = true) {
    setLoadingModels(true);
    if (showErrors) {
      setModelError('');
      setError('');
    }

    try {
      const [llmResult, piperResult] = await Promise.allSettled([
        llmClient.models(),
        piperClient.models()
      ]);

      if (llmResult.status === 'fulfilled') {
        setLlmModels(llmResult.value);
        if (!selectedLlmModel && llmResult.value[0]?.id) {
          updateSettings({ llmModel: llmResult.value[0].id });
        }
      } else if (showErrors) {
        setModelError(llmResult.reason instanceof LlmApiError ? llmResult.reason.message : 'No se pudieron listar los modelos LLM. Puedes escribir el modelo manualmente.');
      }

      if (piperResult.status === 'fulfilled') {
        setPiperModels(piperResult.value.models ?? []);
      } else if (showErrors) {
        setModelError((prev) => prev || 'No se pudieron obtener los modelos de Piper Neo.');
      }
    } finally {
      setLoadingModels(false);
    }
  }

  async function synthesizeAssistant(message: ChatMessage, sourceMessages = messages, speechText = stripMarkdownForSpeech(message.content)) {
    if (!settings.selectedModel) {
      setError('Selecciona un modelo de voz Piper antes de reproducir respuestas.');
      return;
    }

    const text = speechText.trim();
    if (!text) {
      setMessages(sourceMessages.map((item) => item.id === message.id ? { ...item, error: 'Respuesta omitida para voz porque solo contenía código o markdown técnico.' } : item));
      return;
    }

    setAudioBusyId(message.id);
    setError('');

    try {
      const shouldSendSpeaker = Boolean(selectedPiperModel && (selectedPiperModel.num_speakers ?? 1) > 1);
      const result = await piperClient.synthesize({
        text,
        model: settings.selectedModel,
        speakerId: shouldSendSpeaker ? settings.speakerId : undefined
      });
      const blob = await piperClient.audio(result.url);
      const audio = {
        file: result.file,
        model: result.model,
        url: result.url,
        format: result.format,
        bytes: result.bytes,
        audioSeconds: result.audio_seconds,
        inferSeconds: result.infer_seconds,
        realTimeFactor: result.real_time_factor,
        createdAt: Date.now()
      };

      setChatAudioBlobs({ ...chatAudioBlobs, [message.id]: blob });
      setMessages(sourceMessages.map((item) => item.id === message.id ? { ...item, audio } : item));
      playBlob(message.id, blob);
    } catch (err) {
      const messageText = err instanceof PiperApiError ? err.message : 'La respuesta llegó, pero Piper Neo no pudo generar el audio.';
      setError(messageText);
      setMessages(sourceMessages.map((item) => item.id === message.id ? { ...item, error: messageText } : item));
    } finally {
      setAudioBusyId('');
    }
  }

  function playBlob(messageId: string, blob: Blob) {
    if (playbackUrl) URL.revokeObjectURL(playbackUrl);
    const url = URL.createObjectURL(blob);
    setPlaybackUrl(url);
    setPlaybackMessageId(messageId);
    setPendingPlay(true);
  }

  async function playMessage(message: ChatMessage) {
    const existing = chatAudioBlobs[message.id];
    if (existing) {
      playBlob(message.id, existing);
      return;
    }

    if (message.audio?.url) {
      setAudioBusyId(message.id);
      try {
        const blob = await piperClient.audio(message.audio.url);
        setChatAudioBlobs({ ...chatAudioBlobs, [message.id]: blob });
        playBlob(message.id, blob);
      } catch (err) {
        setError(err instanceof PiperApiError ? err.message : 'No se pudo recuperar el audio guardado.');
      } finally {
        setAudioBusyId('');
      }
      return;
    }

    await synthesizeAssistant(message);
  }

  async function completeChat(nextMessages: ChatMessage[]) {
    const model = activeLlmModel(settings);
    if (!model) {
      setError('Selecciona o escribe manualmente un modelo LLM.');
      return;
    }
    if (!settings.selectedModel) {
      setError('Selecciona un modelo de voz Piper para poder reproducir la respuesta del agente.');
      return;
    }

    const assistantId = makeId('assistant');
    const assistantMessage: ChatMessage = {
      id: assistantId,
      role: 'assistant',
      content: '',
      createdAt: Date.now(),
      llmModel: model,
      piperModel: settings.selectedModel
    };
    const withPlaceholder = [...nextMessages, assistantMessage];

    setBusy(true);
    setError('');
    setMessages(withPlaceholder);

    let streamed = '';

    try {
      const finalAnswer = await llmClient.chatCompletionStream({
        model,
        messages: buildOpenAiMessages(nextMessages, settings),
        temperature: settings.llmTemperature,
        maxTokens: settings.llmMaxTokens,
        onDelta: (delta) => {
          streamed += delta;
          setMessages((current) => current.map((item) => item.id === assistantId ? { ...item, content: streamed } : item));
        }
      });

      const answer = finalAnswer || streamed;
      const finalMessage: ChatMessage = { ...assistantMessage, content: answer.trim() };
      const finalMessages = [...nextMessages, finalMessage];
      setMessages(finalMessages);
      await synthesizeAssistant(finalMessage, finalMessages, stripMarkdownForSpeech(answer));
    } catch (err) {
      setMessages(nextMessages);
      setError(err instanceof LlmApiError ? err.message : 'No se pudo obtener respuesta del modelo LLM.');
    } finally {
      setBusy(false);
    }
  }

  async function sendMessage() {
    const content = draft.trim();
    if (!content) return;

    const userMessage: ChatMessage = {
      id: makeId('user'),
      role: 'user',
      content,
      createdAt: Date.now()
    };
    const nextMessages = [...messages, userMessage];
    setDraft('');
    enableAutoScroll('auto');
    setMessages(nextMessages);
    await completeChat(nextMessages);
  }

  async function regenerateLastAnswer() {
    if (busy) return;
    const lastAssistantIndex = [...messages].reverse().findIndex((message) => message.role === 'assistant');
    if (lastAssistantIndex === -1) {
      setError('Todavía no hay una respuesta del agente para regenerar.');
      return;
    }

    const actualIndex = messages.length - 1 - lastAssistantIndex;
    const previousMessages = messages.slice(0, actualIndex);
    const removedAssistant = messages[actualIndex];
    const nextBlobs = { ...chatAudioBlobs };
    delete nextBlobs[removedAssistant.id];
    setChatAudioBlobs(nextBlobs);
    enableAutoScroll('auto');
    setMessages(previousMessages);
    await completeChat(previousMessages);
  }

  function newChat() {
    clearChatMessages();
    setChatAudioBlobs({});
    setMessages([]);
    setDraft('');
    setError('');
    if (playbackUrl) URL.revokeObjectURL(playbackUrl);
    setPlaybackUrl('');
    setPlaybackMessageId('');
    enableAutoScroll('auto');
  }

  return (
    <section className={`chatgpt-layout ${showSettings ? 'settings-open' : ''}`}>
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />

      <header className="chatgpt-topbar">
        <button className="icon-text-button" onClick={() => navigate('/')}>
          <Icon name="home" />
          <span>Salir</span>
        </button>
        <div className="chatgpt-status-strip">
          <span>{selectedLlmModel || 'LLM sin configurar'}</span>
          <span>{settings.selectedModel || 'Voz sin seleccionar'}</span>
        </div>
        <div className="chatgpt-actions">
          <button className="ghost-icon-button" onClick={() => void refreshAllModels(true)} disabled={loadingModels} title="Cargar modelos">
            <Icon name={loadingModels ? 'loader' : 'models'} className={loadingModels ? 'spin' : undefined} />
          </button>
          <button className="ghost-icon-button" onClick={regenerateLastAnswer} disabled={busy || !messages.some((message) => message.role === 'assistant')} title="Regenerar">
            <Icon name="refresh" />
          </button>
          <button className="ghost-icon-button" onClick={newChat} disabled={busy || messages.length === 0} title="Nuevo chat">
            <Icon name="chat" />
          </button>
          <button className="ghost-icon-button" onClick={() => setShowSettings((value) => !value)} title="Ajustes">
            <Icon name="settings" />
          </button>
        </div>
      </header>

      <main className="chatgpt-main">
        {(error || modelError) && (
          <div className="chatgpt-alerts">
            {error && <StatusPill type="danger">{error}</StatusPill>}
            {modelError && <StatusPill type="warn">{modelError}</StatusPill>}
          </div>
        )}

        <div className="chatgpt-scroll" ref={scrollRef} onScroll={handleChatScroll} aria-live="polite">
          {messages.length === 0 && (
            <div className="chatgpt-empty">
              <Icon name="spark" />
              <span>Escribe para iniciar.</span>
            </div>
          )}

          {messages.map((message) => (
            <article key={message.id} className={`chatgpt-message ${message.role}`}>
              <div className="chatgpt-avatar">{message.role === 'assistant' ? 'AI' : 'Tú'}</div>
              <div className="chatgpt-content">
                {message.content ? <MarkdownMessage content={message.content} /> : <div className="stream-caret"><span /> <span /> <span /></div>}
                {message.role === 'assistant' && message.content && (
                  <div className="chatgpt-message-actions">
                    <button className="secondary-button small-button" onClick={() => void playMessage(message)} disabled={audioBusyId === message.id}>
                      <Icon name={audioBusyId === message.id ? 'loader' : 'play'} className={audioBusyId === message.id ? 'spin' : undefined} />
                      {playbackMessageId === message.id ? 'Reproducir otra vez' : 'Play'}
                    </button>
                    {message.audio && <small>{formatSeconds(message.audio.audioSeconds)} · {formatBytes(message.audio.bytes)}</small>}
                    {message.error && <small className="danger-text">{message.error}</small>}
                  </div>
                )}
              </div>
            </article>
          ))}

          {busy && messages[messages.length - 1]?.role !== 'assistant' && (
            <article className="chatgpt-message assistant">
              <div className="chatgpt-avatar">AI</div>
              <div className="chatgpt-content"><div className="stream-caret"><span /> <span /> <span /></div></div>
            </article>
          )}
          <div ref={listEndRef} />
        </div>

        {!autoScrollEnabled && (
          <button
            className="chatgpt-scroll-bottom"
            type="button"
            onClick={() => enableAutoScroll('smooth')}
            title="Volver al último mensaje"
          >
            ↓ Último mensaje
          </button>
        )}
      </main>

      <footer className="chatgpt-composer-wrap">
        <div className="chatgpt-player-row">
          <audio ref={audioRef} controls src={playbackUrl || undefined} />
        </div>
        <div className="chatgpt-composer">
          <textarea
            ref={textareaRef}
            value={draft}
            onChange={(event) => setDraft(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter' && !event.shiftKey) {
                event.preventDefault();
                if (canSend) void sendMessage();
              }
            }}
            placeholder="Escribe un mensaje..."
          />
          <button className="primary-button" onClick={() => void sendMessage()} disabled={!canSend}>
            <Icon name={busy ? 'loader' : 'chat'} className={busy ? 'spin' : undefined} />
          </button>
        </div>
      </footer>

      {showSettings && (
        <>
          <button
            className="chatgpt-settings-backdrop"
            type="button"
            aria-label="Cerrar ajustes"
            onClick={() => setShowSettings(false)}
          />
          <aside className="chatgpt-settings-drawer panel" aria-modal="true" role="dialog">
            <div className="drawer-head">
              <button className="ghost-icon-button" onClick={() => setShowSettings(false)} title="Cerrar ajustes"><Icon name="close" /></button>
            </div>

            <div className="drawer-grid">
          <label className="field">
            <span>URL base</span>
            <input value={settings.llmApiUrl} onChange={(event) => updateSettings({ llmApiUrl: event.target.value })} placeholder="http://127.0.0.1:11434/v1" />
            <small className="field-hint">Usa solo la base compatible /v1.</small>
          </label>

          <label className="toggle-row">
            <input type="checkbox" checked={settings.llmUseToken} onChange={(event) => updateSettings({ llmUseToken: event.target.checked })} />
            <span>Usar API key<small>Authorization: Bearer.</small></span>
          </label>

          {settings.llmUseToken && (
            <label className="field">
              <span>API key/token</span>
              <input type="password" value={settings.llmToken} onChange={(event) => updateSettings({ llmToken: event.target.value })} placeholder="sk-..." />
            </label>
          )}

          <div className="drawer-two-cols">
            <label className="field">
              <span>Modelo listado</span>
              <select value={settings.llmModel} onChange={(event) => updateSettings({ llmModel: event.target.value, llmManualModel: '' })}>
                <option value="">Seleccionar...</option>
                {llmModels.map((model) => <option key={model.id} value={model.id}>{model.id}</option>)}
              </select>
            </label>

            <label className="field">
              <span>Modelo manual</span>
              <input value={settings.llmManualModel} onChange={(event) => updateSettings({ llmManualModel: event.target.value, llmModel: '' })} placeholder="qwen, llama, gpt..." />
            </label>
          </div>

          <label className="field">
            <span>System prompt</span>
            <textarea className="compact-textarea" value={settings.llmSystemPrompt} onChange={(event) => updateSettings({ llmSystemPrompt: event.target.value })} />
          </label>

          <div className="drawer-three-cols">
            <label className="field">
              <span>Contexto</span>
              <input type="number" min={2} max={40} value={settings.llmContextMessages} onChange={(event) => updateSettings({ llmContextMessages: Number(event.target.value) })} />
            </label>
            <label className="field">
              <span>Temperatura</span>
              <input type="number" min={0} max={2} step={0.1} value={settings.llmTemperature} onChange={(event) => updateSettings({ llmTemperature: Number(event.target.value) })} />
            </label>
            <label className="field">
              <span>Max tokens</span>
              <input type="number" min={0} max={8192} value={settings.llmMaxTokens} onChange={(event) => updateSettings({ llmMaxTokens: Number(event.target.value) })} />
            </label>
          </div>

          <div className="model-details compact-model-details">
            <p>{selectedPiperModel ? modelDisplayName(selectedPiperModel) : settings.selectedModel || 'Sin voz'}</p>
            <button className="secondary-button full" onClick={() => navigate('/models')}><Icon name="models" /> Cambiar voz</button>
          </div>
            </div>
          </aside>
        </>
      )}
    </section>
  );
}
