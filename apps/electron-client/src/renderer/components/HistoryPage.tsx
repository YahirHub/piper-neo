import { useEffect, useState } from 'react';
import type { AudioHistoryItem } from '../lib/types';
import { clearHistory, deleteHistoryItem, listHistory } from '../lib/historyDb';
import { Icon } from '../lib/icons';
import { formatBytes, formatSeconds, shortDate } from '../lib/format';
import { useObjectUrl } from '../lib/objectUrl';
import { StatusPill } from './StatusPill';

function HistoryRow({ item, onDelete }: { item: AudioHistoryItem; onDelete: () => void }) {
  const audioUrl = useObjectUrl(item.blob);

  function download() {
    const anchor = document.createElement('a');
    anchor.href = audioUrl;
    anchor.download = item.file || 'piper-neo.wav';
    anchor.click();
  }

  return (
    <article className="history-row panel">
      <div className="history-top">
        <div>
          <strong>{item.modelName || item.model}</strong>
          <small>{shortDate(item.createdAt)} · {formatSeconds(item.audioSeconds)} · {formatBytes(item.bytes)}</small>
        </div>
        <div className="actions-row">
          <button className="secondary-button" onClick={download}><Icon name="download" /> Descargar</button>
          <button className="icon-button danger" onClick={onDelete} aria-label="Eliminar audio"><Icon name="trash" /></button>
        </div>
      </div>
      <p>{item.text}</p>
      <audio controls src={audioUrl} />
    </article>
  );
}

export function HistoryPage() {
  const [items, setItems] = useState<AudioHistoryItem[]>([]);
  const [loading, setLoading] = useState(true);

  async function refresh() {
    setLoading(true);
    setItems(await listHistory());
    setLoading(false);
  }

  useEffect(() => {
    void refresh();
  }, []);

  async function remove(id: string) {
    await deleteHistoryItem(id);
    await refresh();
  }

  async function clearAll() {
    await clearHistory();
    await refresh();
  }

  return (
    <section className="page-stack">
      <header className="page-header split">
        <div>
          <StatusPill type="muted">Persistente y local</StatusPill>
          <h1>Historial de audios</h1>
          <p>Los audios se guardan en tu equipo para no perderlos al cambiar de página o reiniciar la app.</p>
        </div>
        <button className="secondary-button danger-text" onClick={clearAll} disabled={items.length === 0}><Icon name="trash" /> Limpiar</button>
      </header>

      {loading && <div className="empty-state"><Icon name="loader" className="spin" /> Cargando historial...</div>}
      {!loading && items.length === 0 && <div className="empty-state"><Icon name="history" /> Todavía no hay audios guardados.</div>}
      <div className="history-list">
        {items.map((item) => <HistoryRow key={item.id} item={item} onDelete={() => void remove(item.id)} />)}
      </div>
    </section>
  );
}
