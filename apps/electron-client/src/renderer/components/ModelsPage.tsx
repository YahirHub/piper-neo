import { useEffect, useMemo, useState } from 'react';
import { PiperApiClient, PiperApiError } from '../lib/api';
import type { AppSettings, PiperModel, RoutePath } from '../lib/types';
import { Icon } from '../lib/icons';
import { ModelCard } from './ModelCard';
import { StatusPill } from './StatusPill';

export function ModelsPage({ settings, updateSettings, navigate }: { settings: AppSettings; updateSettings: (next: Partial<AppSettings>) => void; navigate: (path: RoutePath) => void }) {
  const client = useMemo(() => new PiperApiClient(settings), [settings.apiUrl, settings.token, settings.useToken]);
  const [models, setModels] = useState<PiperModel[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [query, setQuery] = useState('');

  async function loadModels() {
    setLoading(true);
    setError('');
    try {
      const payload = await client.models();
      setModels(payload.models ?? []);
    } catch (err) {
      setError(err instanceof PiperApiError ? err.message : 'No se pudieron cargar los modelos.');
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void loadModels();
  }, [client]);

  const filtered = models.filter((model) => {
    const text = `${model.file} ${model.name ?? ''} ${model.modelcard?.name ?? ''} ${model.modelcard?.description ?? ''} ${model.modelcard?.language ?? ''}`.toLowerCase();
    return text.includes(query.toLowerCase());
  });

  function select(model: PiperModel) {
    updateSettings({ selectedModel: model.file, lastRoute: '/' });
    navigate('/');
  }

  return (
    <section className="page-stack">
      <header className="page-header split">
        <div>
          <StatusPill type="muted">Modelos desde {client.baseUrl}</StatusPill>
          <h1>Selecciona una voz</h1>
          <p>Se cargan con metadata completa e imagen desde la API local de Piper Neo.</p>
        </div>
        <button className="secondary-button" onClick={loadModels} disabled={loading}>
          <Icon name={loading ? 'loader' : 'models'} className={loading ? 'spin' : undefined} />
          Recargar
        </button>
      </header>

      <div className="toolbar glass">
        <div className="input-with-icon grow">
          <Icon name="models" />
          <input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Buscar por nombre, idioma o archivo..." />
        </div>
        <span>{filtered.length} / {models.length} modelos</span>
      </div>

      {error && <StatusPill type="danger">{error}</StatusPill>}
      {loading && <div className="empty-state"><Icon name="loader" className="spin" /> Cargando modelos...</div>}
      {!loading && filtered.length === 0 && <div className="empty-state"><Icon name="alert" /> No hay modelos disponibles en el servidor.</div>}

      <div className="models-grid">
        {filtered.map((model) => (
          <ModelCard key={model.file} model={model} client={client} selected={settings.selectedModel === model.file} onSelect={() => select(model)} />
        ))}
      </div>
    </section>
  );
}
