import type { ComponentType, SVGProps } from 'react';
import {
  AlertTriangle,
  Box,
  Check,
  Copy,
  Download,
  History,
  Home,
  Image,
  KeyRound,
  Link,
  Loader2,
  MonitorPlay,
  MessageCircle,
  Mic2,
  Pause,
  Play,
  RefreshCw,
  Server,
  Settings,
  ShieldCheck,
  Sparkles,
  Trash2,
  Volume2,
  Waves,
  X
} from 'lucide-react';

export type IconName =
  | 'spark'
  | 'server'
  | 'shield'
  | 'mic'
  | 'play'
  | 'pause'
  | 'wave'
  | 'settings'
  | 'models'
  | 'history'
  | 'check'
  | 'alert'
  | 'download'
  | 'trash'
  | 'key'
  | 'link'
  | 'loader'
  | 'home'
  | 'volume'
  | 'image'
  | 'close'
  | 'chat'
  | 'refresh'
  | 'copy'
  | 'preview';

const icons: Record<IconName, ComponentType<SVGProps<SVGSVGElement>>> = {
  spark: Sparkles,
  server: Server,
  shield: ShieldCheck,
  mic: Mic2,
  play: Play,
  pause: Pause,
  wave: Waves,
  settings: Settings,
  models: Box,
  history: History,
  check: Check,
  alert: AlertTriangle,
  download: Download,
  trash: Trash2,
  key: KeyRound,
  link: Link,
  loader: Loader2,
  home: Home,
  volume: Volume2,
  image: Image,
  close: X,
  chat: MessageCircle,
  refresh: RefreshCw,
  copy: Copy,
  preview: MonitorPlay
};

export function Icon({ name, className, ...props }: { name: IconName; className?: string } & SVGProps<SVGSVGElement>) {
  const LucideIcon = icons[name];
  return (
    <LucideIcon
      className={className}
      aria-hidden="true"
      strokeWidth={1.9}
      {...props}
    />
  );
}
