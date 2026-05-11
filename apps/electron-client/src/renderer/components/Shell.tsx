import type { ReactNode } from 'react';
import type { RoutePath } from '../lib/types';
import { Icon, type IconName } from '../lib/icons';
import { Brand } from './Brand';

const nav: Array<{ path: RoutePath; label: string; icon: IconName }> = [
  { path: '/', label: 'Estudio', icon: 'mic' },
  { path: '/models', label: 'Modelos', icon: 'models' },
  { path: '/chat', label: 'Live chat', icon: 'chat' },
  { path: '/history', label: 'Historial', icon: 'history' },
  { path: '/settings', label: 'Ajustes', icon: 'settings' }
];

export function Shell({
  route,
  connected,
  selectedModel,
  children,
  navigate
}: {
  route: RoutePath;
  connected: boolean;
  selectedModel?: string;
  children: ReactNode;
  navigate: (path: RoutePath) => void;
}) {
  const showNav = connected && route !== '/setup';

  return (
    <div className="app-shell">
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />
      {showNav && (
        <aside className="sidebar">
          <Brand />
          <nav className="nav-stack" aria-label="Navegación principal">
            {nav.map((item) => (
              <button key={item.path} className={`nav-item ${route === item.path ? 'active' : ''}`} onClick={() => navigate(item.path)}>
                <Icon name={item.icon} />
                <span>{item.label}</span>
              </button>
            ))}
          </nav>
          <div className="sidebar-footer">
            <div className="sidebar-card">
              <small>Modelo activo</small>
              <strong>{selectedModel || 'Sin seleccionar'}</strong>
            </div>
            <a className="repo-link" href="https://github.com/YahirHub/piper-neo" target="_blank" rel="noreferrer">
              github.com/YahirHub/piper-neo
            </a>
          </div>
        </aside>
      )}
      <main className={showNav ? 'main-content' : 'main-content main-centered'}>{children}</main>
    </div>
  );
}
