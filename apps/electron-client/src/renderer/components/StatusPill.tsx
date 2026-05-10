import type { ReactNode } from 'react';
import { Icon } from '../lib/icons';

export function StatusPill({ type, children }: { type: 'ok' | 'warn' | 'danger' | 'muted'; children: ReactNode }) {
  return (
    <span className={`status-pill ${type}`}>
      <Icon name={type === 'ok' ? 'check' : type === 'danger' ? 'alert' : 'spark'} />
      {children}
    </span>
  );
}
