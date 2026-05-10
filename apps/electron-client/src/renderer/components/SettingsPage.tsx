import { useMemo, useState } from 'react';
import { PiperApiClient, PiperApiError } from '../lib/api';
import { resetSettings } from '../lib/settings';
import type { AppSettings, RoutePath } from '../lib/types';
import { Icon } from '../lib/icons';
import { StatusPill } from './StatusPill';

export function SettingsPage({ settings, updateSettings, replaceSettings, navigate }: { settings: AppSettings; updateSettings: (next: Partial<AppSettings>) => void; replaceSettings: (next: AppSettings) => void; navigate: (path: RoutePath) => void }) {
  const [apiUrl, setApiUrl] = useState(settings.apiUrl);
  const [token, setToken] = useState(settings.token);
  const [useToken, setUseToken] = useState(settings.useToken);
  const [checking, setChecking] = useState(false);
  const [message, setMessage] = useState<{ type: 'ok' | 'danger' | 'warn'; text: string } | null>(null);
  const client = useMemo(() => new PiperApiClient({ apiUrl, token, useToken }), [apiUrl, token, useToken]);

  async function saveAndCheck() {
    setChecking(true);
    setMessage(null);
    try {
      await client.check();
      updateSettings({ apiUrl: client.baseUrl, token, useToken, connected: true });
      setMessage({ type: 'ok', text: 'Configuración guardada y servidor verificado.' });
    } catch (error) {
      const text = error instanceof PiperApiError ? error.message : 'No se pudo comprobar el servidor.';
      setMessage({ type: 'danger', text });
    } finally {
      setChecking(false);
    }
  }

  function resetAll() {
    const next = resetSettings();
    replaceSettings(next);
    navigate('/setup');
  }

  return (
    <section className="settings-grid">
      <div className="page-stack">
        <header className="page-header">
          <StatusPill type="muted">Cliente local</StatusPill>
          <h1>Ajustes</h1>
          <p>Modifica conexión, token y preferencias generales de Piper Neo Client.</p>
        </header>

        <div className="panel settings-panel">
          <h2>Conexión API</h2>
          <label className="field">
            <span>URL</span>
            <div className="input-with-icon">
              <Icon name="link" />
              <input value={apiUrl} onChange={(event) => setApiUrl(event.target.value)} />
            </div>
          </label>
          <label className="toggle-row">
            <input type="checkbox" checked={useToken} onChange={(event) => setUseToken(event.target.checked)} />
            <span><strong>Usar token</strong><small>Authorization: Bearer &lt;token&gt;</small></span>
          </label>
          {useToken && (
            <label className="field">
              <span>Token</span>
              <div className="input-with-icon">
                <Icon name="key" />
                <input type="password" value={token} onChange={(event) => setToken(event.target.value)} />
              </div>
            </label>
          )}
          {message && <StatusPill type={message.type === 'ok' ? 'ok' : message.type === 'danger' ? 'danger' : 'warn'}>{message.text}</StatusPill>}
          <button className="primary-button" disabled={checking} onClick={saveAndCheck}>
            <Icon name={checking ? 'loader' : 'check'} className={checking ? 'spin' : undefined} />
            Guardar y comprobar
          </button>
        </div>
      </div>

      <aside className="panel settings-side">
        <h2>Preferencias</h2>
        <label className="field">
          <span>Modelo seleccionado</span>
          <input value={settings.selectedModel || 'Sin seleccionar'} readOnly />
        </label>
        <label className="field">
          <span>Speaker ID por defecto</span>
          <input type="number" min={0} value={settings.speakerId} onChange={(event) => updateSettings({ speakerId: Number(event.target.value || 0) })} />
        </label>
        <label className="toggle-row">
          <input type="checkbox" checked={settings.autoPlay} onChange={(event) => updateSettings({ autoPlay: event.target.checked })} />
          <span><strong>Auto reproducción</strong><small>Reproducir al terminar.</small></span>
        </label>
        <label className="toggle-row">
          <input type="checkbox" checked={settings.saveHistory} onChange={(event) => updateSettings({ saveHistory: event.target.checked })} />
          <span><strong>Guardar historial</strong><small>Audios en IndexedDB.</small></span>
        </label>
        <button className="secondary-button danger-text full" onClick={resetAll}><Icon name="trash" /> Reiniciar configuración</button>
      </aside>
    </section>
  );
}
