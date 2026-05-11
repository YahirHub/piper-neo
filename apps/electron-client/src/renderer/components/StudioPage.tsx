import { useEffect, useMemo, useRef, useState } from 'react';
import { PiperApiClient, PiperApiError } from '../lib/api';
import { loadDraft, saveDraft } from '../lib/settings';
import type { AppSettings, AudioHistoryItem, CurrentAudioState, PiperModel, RoutePath } from '../lib/types';
import { Icon } from '../lib/icons';
import { formatBytes, formatSeconds, modelDisplayName } from '../lib/format';
import { saveHistoryItem } from '../lib/historyDb';
import { StatusPill } from './StatusPill';

export function StudioPage({
  settings,
  updateSettings,
  navigate,
  currentAudio,
  setCurrentAudio
}: {
  settings: AppSettings;
  updateSettings: (next: Partial<AppSettings>) => void;
  navigate: (path: RoutePath) => void;
  currentAudio: CurrentAudioState;
  setCurrentAudio: (audio: CurrentAudioState) => void;
}) {
  const client = useMemo(() => new PiperApiClient(settings), [settings.apiUrl, settings.token, settings.useToken]);
  const [text, setText] = useState(loadDraft());
  const [models, setModels] = useState<PiperModel[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [lastAudioUrl, setLastAudioUrl] = useState('');
  const [showSettings, setShowSettings] = useState(false);
  const [pendingAutoPlay, setPendingAutoPlay] = useState(false);
  const [playerPlaying, setPlayerPlaying] = useState(false);
  const [playerCurrentTime, setPlayerCurrentTime] = useState(0);
  const [playerDuration, setPlayerDuration] = useState(0);
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const audioRef = useRef<HTMLAudioElement | null>(null);

  useEffect(() => {
    saveDraft(text);
    const textarea = textareaRef.current;
    if (!textarea) return;
    textarea.style.height = 'auto';
    textarea.style.height = `${Math.min(textarea.scrollHeight, 420)}px`;
  }, [text]);

  useEffect(() => {
    let mounted = true;
    async function load() {
      try {
        const payload = await client.models();
        if (mounted) setModels(payload.models ?? []);
      } catch {
        if (mounted) setModels([]);
      }
    }
    void load();
    return () => { mounted = false; };
  }, [client]);

  useEffect(() => {
    if (!currentAudio.blob) {
      setLastAudioUrl('');
      return;
    }

    const url = URL.createObjectURL(currentAudio.blob);
    setLastAudioUrl(url);

    return () => URL.revokeObjectURL(url);
  }, [currentAudio.blob]);

  useEffect(() => {
    if (!showSettings) return;

    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') setShowSettings(false);
    }

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [showSettings]);

  useEffect(() => {
    if (!pendingAutoPlay || !lastAudioUrl) return;
    setPendingAutoPlay(false);
    window.setTimeout(() => void audioRef.current?.play(), 100);
  }, [pendingAutoPlay, lastAudioUrl]);

  const selectedModel = models.find((model) => model.file === settings.selectedModel);

  function formatPlayerTime(seconds: number) {
    if (!Number.isFinite(seconds) || seconds < 0) return '0:00';
    const minutes = Math.floor(seconds / 60);
    const remaining = Math.floor(seconds % 60).toString().padStart(2, '0');
    return `${minutes}:${remaining}`;
  }

  function togglePlayback() {
    const audio = audioRef.current;
    if (!audio || !lastAudioUrl) return;
    if (audio.paused) {
      void audio.play();
    } else {
      audio.pause();
    }
  }

  function seekPlayer(value: number) {
    const audio = audioRef.current;
    if (!audio) return;
    audio.currentTime = value;
    setPlayerCurrentTime(value);
  }

  async function synthesize() {
    if (!settings.selectedModel) {
      navigate('/models');
      return;
    }
    if (!text.trim()) {
      setError('Escribe un texto antes de convertir.');
      return;
    }

    setBusy(true);
    setError('');
    try {
      const shouldSendSpeaker = Boolean(selectedModel && (selectedModel.num_speakers ?? 1) > 1);
      const result = await client.synthesize({
        text: text.trim(),
        model: settings.selectedModel,
        speakerId: shouldSendSpeaker ? settings.speakerId : undefined
      });
      const blob = await client.audio(result.url);
      setCurrentAudio({
        result,
        blob,
        text: text.trim(),
        modelName: selectedModel ? modelDisplayName(selectedModel) : result.model,
        createdAt: Date.now()
      });

      if (settings.saveHistory) {
        const item: AudioHistoryItem = {
          id: `${Date.now()}-${crypto.randomUUID()}`,
          createdAt: Date.now(),
          text: text.trim(),
          model: result.model,
          modelName: selectedModel ? modelDisplayName(selectedModel) : result.model,
          blob,
          file: result.file,
          format: result.format,
          bytes: result.bytes,
          audioSeconds: result.audio_seconds,
          inferSeconds: result.infer_seconds,
          realTimeFactor: result.real_time_factor
        };
        await saveHistoryItem(item);
      }

      if (settings.autoPlay) {
        setPendingAutoPlay(true);
      }
    } catch (err) {
      setError(err instanceof PiperApiError ? err.message : 'No se pudo generar el audio.');
    } finally {
      setBusy(false);
    }
  }

  function downloadCurrent() {
    if (!lastAudioUrl || !currentAudio.result) return;
    const anchor = document.createElement('a');
    anchor.href = lastAudioUrl;
    anchor.download = currentAudio.result.file || 'piper-neo.wav';
    anchor.click();
  }

  return (
    <section className={`studio-layout studio-redesign ${showSettings ? 'settings-open' : ''}`}>
      <div className="studio-main page-stack">
        <header className="studio-hero panel">
          <div className="studio-hero-copy">
            <StatusPill type={settings.selectedModel ? 'ok' : 'warn'}>{settings.selectedModel ? `Voz activa: ${settings.selectedModel}` : 'Selecciona un modelo'}</StatusPill>
            <h1>Estudio</h1>
            <p>Convierte texto a voz localmente con Piper Neo. El texto y el último audio se conservan al cambiar de pantalla.</p>
          </div>
          <div className="studio-hero-actions">
            <button className="secondary-button" onClick={() => navigate('/models')}><Icon name="models" /> Modelos</button>
            <button className={`secondary-button ${showSettings ? 'active-action' : ''}`} onClick={() => setShowSettings((value) => !value)}><Icon name="settings" /> {showSettings ? 'Cerrar' : 'Ajustes'}</button>
          </div>
        </header>

        <div className="studio-workspace">
          <div className="composer panel studio-composer-card">
            <div className="studio-section-label">
              <span>Texto a sintetizar</span>
              <small>{new Blob([text]).size.toLocaleString('es-MX')} bytes · {text.trim().split(/\s+/).filter(Boolean).length} palabras</small>
            </div>
            <textarea ref={textareaRef} value={text} onChange={(event) => setText(event.target.value)} placeholder="Escribe aquí el texto que quieres convertir a voz..." />
            <div className="composer-footer studio-composer-footer">
              <span>{selectedModel ? modelDisplayName(selectedModel) : 'Sin modelo seleccionado'}</span>
              <button className="primary-button studio-convert-button" disabled={busy || !text.trim()} onClick={synthesize}>
                <Icon name={busy ? 'loader' : 'play'} className={busy ? 'spin' : undefined} />
                {busy ? 'Convirtiendo...' : 'Convertir a audio'}
              </button>
            </div>
          </div>

          <div className="audio-panel panel studio-player-card">
            <div className="audio-header">
              <div>
                <strong>Reproductor</strong>
                <small>{currentAudio.result ? `${formatSeconds(currentAudio.result.audio_seconds)} · ${formatBytes(currentAudio.result.bytes)} · RTF ${currentAudio.result.real_time_factor?.toFixed(2) ?? '—'}` : 'El audio aparecerá aquí automáticamente.'}</small>
              </div>
              <button className="secondary-button" disabled={!lastAudioUrl} onClick={downloadCurrent}><Icon name="download" /> Descargar</button>
            </div>

            <audio
              ref={audioRef}
              src={lastAudioUrl || undefined}
              onLoadedMetadata={(event) => setPlayerDuration(event.currentTarget.duration || 0)}
              onTimeUpdate={(event) => setPlayerCurrentTime(event.currentTarget.currentTime || 0)}
              onPlay={() => setPlayerPlaying(true)}
              onPause={() => setPlayerPlaying(false)}
              onEnded={() => setPlayerPlaying(false)}
            />

            <div className={`custom-audio-player ${playerPlaying ? 'playing' : ''}`}>
              <button className="player-main-button" disabled={!lastAudioUrl} onClick={togglePlayback} aria-label={playerPlaying ? 'Pausar audio' : 'Reproducir audio'}>
                <Icon name={playerPlaying ? 'pause' : 'play'} />
              </button>
              <div className="player-body">
                <div className="player-title-row">
                  <span>{currentAudio.modelName || 'Última generación'}</span>
                  <small>{formatPlayerTime(playerCurrentTime)} / {formatPlayerTime(playerDuration || currentAudio.result?.audio_seconds || 0)}</small>
                </div>
                <input
                  className="player-range"
                  type="range"
                  min={0}
                  max={playerDuration || currentAudio.result?.audio_seconds || 0}
                  step={0.01}
                  value={Math.min(playerCurrentTime, playerDuration || currentAudio.result?.audio_seconds || 0)}
                  disabled={!lastAudioUrl}
                  onChange={(event) => seekPlayer(Number(event.target.value))}
                />
              </div>
              <div className="player-bars" aria-hidden="true"><i /><i /><i /><i /></div>
            </div>

            {currentAudio.result && (
              <p className="audio-note">
                Conservado en esta sesión: si cambias de página y vuelves al estudio, este audio seguirá disponible.
              </p>
            )}
          </div>
        </div>

        {error && <StatusPill type="danger">{error}</StatusPill>}
      </div>

      {showSettings && (
        <aside className="studio-side panel slide-in-panel" aria-label="Ajustes de síntesis">
          <div className="studio-side-header">
            <div>
              <h2>Ajustes de síntesis</h2>
              <small>Presiona Esc o la X para cerrar.</small>
            </div>
            <button className="icon-button" type="button" onClick={() => setShowSettings(false)} aria-label="Cerrar ajustes">
              <Icon name="close" />
            </button>
          </div>
          <label className="field">
            <span>Speaker ID</span>
            <input
              type="number"
              min={0}
              disabled={Boolean(selectedModel && (selectedModel.num_speakers ?? 1) <= 1)}
              value={settings.speakerId}
              onChange={(event) => updateSettings({ speakerId: Number(event.target.value || 0) })}
            />
            <small className="field-hint">
              {selectedModel && (selectedModel.num_speakers ?? 1) <= 1
                ? 'Este modelo es de una sola voz; no se enviará speaker_id para evitar el error Invalid Feed Input Name:sid.'
                : 'Solo se envía a la API cuando el modelo tiene varios speakers.'}
            </small>
          </label>
          <label className="toggle-row">
            <input type="checkbox" checked={settings.autoPlay} onChange={(event) => updateSettings({ autoPlay: event.target.checked })} />
            <span><strong>Reproducir automáticamente</strong><small>Al terminar la conversión.</small></span>
          </label>
          <label className="toggle-row">
            <input type="checkbox" checked={settings.saveHistory} onChange={(event) => updateSettings({ saveHistory: event.target.checked })} />
            <span><strong>Guardar historial local</strong><small>Se guarda en IndexedDB, no sale de tu PC.</small></span>
          </label>

          {selectedModel && (
            <div className="model-details">
              <h3>{modelDisplayName(selectedModel)}</h3>
              <p>{selectedModel.modelcard?.description || selectedModel.modelcard?.voiceprompt || 'Sin descripción.'}</p>
              <dl>
                <div><dt>Idioma</dt><dd>{selectedModel.modelcard?.language || selectedModel.language || '—'}</dd></div>
                <div><dt>Calidad</dt><dd>{selectedModel.audio?.quality || '—'}</dd></div>
                <div><dt>Sample rate</dt><dd>{selectedModel.audio?.sample_rate || '—'}</dd></div>
                <div><dt>Speakers</dt><dd>{selectedModel.num_speakers ?? 1}</dd></div>
              </dl>
            </div>
          )}
        </aside>
      )}
    </section>
  );
}
