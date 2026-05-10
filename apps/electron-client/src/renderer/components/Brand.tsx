import { Icon } from '../lib/icons';

export function Brand({ compact = false }: { compact?: boolean }) {
  return (
    <div className={compact ? 'brand brand-compact' : 'brand'}>
      <div className="brand-mark" aria-hidden="true">
        <Icon name="wave" />
      </div>
      <div>
        <strong>Piper Neo</strong>
        {!compact && <span>Desktop Client</span>}
      </div>
    </div>
  );
}
