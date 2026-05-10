import type { ReactElement, SVGProps } from 'react';

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
  | 'close';

const paths: Record<IconName, ReactElement> = {
  spark: <path d="M12 2l1.7 5.2L19 9l-5.3 1.8L12 16l-1.7-5.2L5 9l5.3-1.8L12 2Zm6 12l.9 2.7L22 18l-3.1 1.3L18 22l-.9-2.7L14 18l3.1-1.3L18 14ZM5 14l.9 2.7L9 18l-3.1 1.3L5 22l-.9-2.7L1 18l3.1-1.3L5 14Z" />,
  server: <path d="M4 5.5A2.5 2.5 0 0 1 6.5 3h11A2.5 2.5 0 0 1 20 5.5v3A2.5 2.5 0 0 1 17.5 11h-11A2.5 2.5 0 0 1 4 8.5v-3Zm0 10A2.5 2.5 0 0 1 6.5 13h11a2.5 2.5 0 0 1 2.5 2.5v3a2.5 2.5 0 0 1-2.5 2.5h-11A2.5 2.5 0 0 1 4 18.5v-3ZM7 7h.01M7 17h.01M10 7h7M10 17h7" />,
  shield: <path d="M12 2.5 20 6v5.7c0 4.8-3.3 8.5-8 9.8-4.7-1.3-8-5-8-9.8V6l8-3.5Zm-3 9 2 2 4-4" />,
  mic: <path d="M12 3a3 3 0 0 0-3 3v6a3 3 0 0 0 6 0V6a3 3 0 0 0-3-3Zm7 8v1a7 7 0 0 1-14 0v-1m7 8v3m-4 0h8" />,
  play: <path d="M8 5v14l11-7L8 5Z" />,
  pause: <path d="M7 5h4v14H7V5Zm6 0h4v14h-4V5Z" />,
  wave: <path d="M3 12h2l2-6 4 12 4-14 3 8h3" />,
  settings: <path d="M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Zm8.5 4a7.7 7.7 0 0 0-.1-1l2-1.5-2-3.5-2.4 1a8.3 8.3 0 0 0-1.7-1l-.3-2.5h-4l-.3 2.5a8.3 8.3 0 0 0-1.7 1l-2.4-1-2 3.5 2 1.5a7.7 7.7 0 0 0 0 2l-2 1.5 2 3.5 2.4-1a8.3 8.3 0 0 0 1.7 1l.3 2.5h4l.3-2.5a8.3 8.3 0 0 0 1.7-1l2.4 1 2-3.5-2-1.5c.1-.3.1-.7.1-1Z" />,
  models: <path d="M12 2 4 6v12l8 4 8-4V6l-8-4Zm0 8 8-4M12 10 4 6m8 4v12" />,
  history: <path d="M3 12a9 9 0 1 0 3-6.7L3 8m0-5v5h5m4-1v6l4 2" />,
  check: <path d="m5 12 4 4L19 6" />,
  alert: <path d="M12 9v4m0 4h.01M10.3 3.9 2.6 17.2A2 2 0 0 0 4.3 20h15.4a2 2 0 0 0 1.7-2.8L13.7 3.9a2 2 0 0 0-3.4 0Z" />,
  download: <path d="M12 3v12m0 0 5-5m-5 5-5-5M5 21h14" />,
  trash: <path d="M4 7h16M10 11v6m4-6v6M6 7l1 14h10l1-14M9 7V4h6v3" />,
  key: <path d="M14 7a5 5 0 1 0-4.4 7.5L12 17h3v3h3v-3h3v-3h-4.5L14 11.5A5 5 0 0 0 14 7Z" />,
  link: <path d="M10 13a5 5 0 0 0 7.1 0l2-2a5 5 0 0 0-7.1-7.1l-1.1 1.1M14 11a5 5 0 0 0-7.1 0l-2 2A5 5 0 0 0 12 20.1l1.1-1.1" />,
  loader: <path d="M21 12a9 9 0 0 1-9 9" />,
  home: <path d="M3 11 12 3l9 8v10h-6v-6H9v6H3V11Z" />,
  volume: <path d="M4 10v4h4l5 5V5L8 10H4Zm13-2a5 5 0 0 1 0 8m2.5-11a9 9 0 0 1 0 14" />,
  image: <path d="M4 5h16v14H4V5Zm3 10 3-3 2 2 3-4 4 5M8 9h.01" />,
  close: <path d="M6 6l12 12M18 6 6 18" />
};

export function Icon({ name, className, ...props }: { name: IconName; className?: string } & SVGProps<SVGSVGElement>) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" {...props}>
      {paths[name]}
    </svg>
  );
}
