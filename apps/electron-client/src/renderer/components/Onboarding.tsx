import { useMemo, useState } from 'react';
import { PiperApiClient, PiperApiError } from '../lib/api';
import type { AppSettings, RoutePath } from '../lib/types';
import { Icon } from '../lib/icons';
import { Brand } from './Brand';
import { StatusPill } from './StatusPill';

export function Onboarding({ settings, updateSettings, navigate }: { settings: AppSettings; updateSettings: (next: Partial<AppSettings>) => void; navigate: (path: RoutePath) => void }) {
  const [apiUrl, setApiUrl] = useState(settings.apiUrl);
  const [useToken, setUseToken] = useState(settings.useToken);
  const [token, setToken] = useState(settings.token);
  const [checking, setChecking] = useState(false);
  const [message, setMessage] = useState<{ type: 'ok' | 'danger' | 'warn'; text: string } | null>(null);

  const client = useMemo(() => new PiperApiClient({ apiUrl, useToken, token }), [apiUrl, token, useToken]);

  async function checkConnection() {
    setChecking(true);
    setMessage(null);
    try {
      const status = await client.check();
      updateSettings({ apiUrl: client.baseUrl, token, useToken, connected: true, lastRoute: '/models' });
      setMessage({ type: 'ok', text: `Conectado correctamente${status.active_model ? ` · activo: ${status.active_model}` : ''}.` });
      window.setTimeout(() => navigate('/models'), 500);
    } catch (error) {
      if (error instanceof PiperApiError) {
        setMessage({ type: error.code === 'invalid_token' ? 'danger' : 'warn', text: error.message });
      } else {
        setMessage({ type: 'danger', text: 'No se pudo comprobar la conexión.' });
      }
    } finally {
      setChecking(false);
    }
  }

  function choosePreset(url: string) {
    setApiUrl(url);
    setMessage(null);
  }

  return (
    <section className="setup-page setup-page-pro">
      <div className="hero-card setup-hero">
        <div className="setup-copy">
          <Brand />
          <h1>Conecta tu servidor local de Piper Neo</h1>
          <p>
            Configura una sola vez la URL de la API, agrega token solo si tu servidor lo usa y el cliente recordará tus modelos, texto y audios generados.
          </p>
          <div className="feature-grid mini setup-feature-river">
            <div><Icon name="server" /> API local</div>
            <div><Icon name="shield" /> Token opcional</div>
            <div><Icon name="history" /> Historial offline</div>
          </div>
          <div className="setup-progress-row" aria-hidden="true">
            <span className="active" />
            <span />
            <span />
          </div>
        </div>
        <div className="orbital-card setup-orbital-pro" aria-hidden="true">
          <span className="orbit-ring ring-a" />
          <span className="orbit-ring ring-b" />
          <div className="orbital-core"><Icon name="wave" /></div>
          <span className="orbit-dot dot-a" />
          <span className="orbit-dot dot-b" />
          <span className="orbit-dot dot-c" />
        </div>
      </div>

      <div className="panel setup-panel">
        <label className="field">
          <span>URL de Piper Neo API</span>
          <div className="input-with-icon">
            <Icon name="link" />
            <input value={apiUrl} onChange={(event) => setApiUrl(event.target.value)} placeholder="http://127.0.0.1:8080" />
          </div>
          <div className="setup-presets">
            <button type="button" onClick={() => choosePreset('http://127.0.0.1:8080')}>Local</button>
            <button type="button" onClick={() => choosePreset('http://localhost:8080')}>localhost</button>
            <button type="button" onClick={() => choosePreset('http://piper-neo:8080')}>Docker</button>
          </div>
        </label>

        <label className="toggle-row">
          <input type="checkbox" checked={useToken} onChange={(event) => setUseToken(event.target.checked)} />
          <span>
            <strong>Usar token/API key</strong>
            <small>Actívalo si iniciaste Piper Neo con --api-token o PIPER_API_TOKEN.</small>
          </span>
        </label>

        {useToken && (
          <label className="field">
            <span>Token</span>
            <div className="input-with-icon">
              <Icon name="key" />
              <input type="password" value={token} onChange={(event) => setToken(event.target.value)} placeholder="Bearer token" />
            </div>
          </label>
        )}

        {message && <StatusPill type={message.type === 'ok' ? 'ok' : message.type === 'danger' ? 'danger' : 'warn'}>{message.text}</StatusPill>}

        <div className={`setup-checklist ${checking ? 'checking' : ''}`}>
          <span><Icon name="link" /> URL</span>
          <span><Icon name="shield" /> Token opcional</span>
          <span><Icon name="check" /> Check</span>
        </div>

        <button className="primary-button full setup-continue" disabled={checking} onClick={checkConnection}>
          {checking ? <Icon name="loader" className="spin" /> : <Icon name="check" />}
          {checking ? 'Comprobando...' : 'Comprobar y continuar'}
        </button>
      </div>
    </section>
  );
}
