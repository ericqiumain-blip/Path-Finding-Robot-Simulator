import type { CSSProperties } from 'react';

export function Icon({ name, size = 18, className = '', style }: { name: string; size?: number; className?: string; style?: CSSProperties }) {
  const paths: Record<string, React.ReactNode> = {
    grid: <><rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/></>,
    chart: <><path d="M4 4v16h17M8 15l4-6 4 3 5-7"/></>,
    bot: <><rect x="4" y="7" width="16" height="13" rx="4"/><path d="M12 3v4M1 12v4M23 12v4M8 16h8"/><circle cx="8" cy="12" r=".7"/><circle cx="16" cy="12" r=".7"/></>,
    box: <><path d="m12 3 9 5v9l-9 5-9-5V8l9-5ZM3 8l9 5 9-5M12 13v9M7.5 5.5l9 5"/></>,
    clock: <><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></>,
    bolt: <path d="m13 2-9 12h7l-1 8 10-13h-8l1-7Z"/>,
    route: <><circle cx="5" cy="5" r="2"/><circle cx="19" cy="19" r="2"/><path d="M7 5h9a4 4 0 0 1 0 8H8a4 4 0 0 0 0 8h7"/></>,
    pause: <><path d="M8 5v14M16 5v14" strokeWidth="3"/></>,
    play: <path d="m8 4 12 8-12 8V4Z"/>,
    step: <><path d="m5 5 10 7-10 7V5ZM19 5v14"/></>,
    reset: <><path d="M4 10a8 8 0 1 1 2 8M4 4v6h6"/></>,
    down: <path d="m6 9 6 6 6-6"/>,
    arrow: <><path d="M4 12h16m-5-5 5 5-5 5"/></>,
    download: <><path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/></>,
    sliders: <><path d="M4 7h5m4 0h7M4 17h10m4 0h2"/><circle cx="11" cy="7" r="2"/><circle cx="16" cy="17" r="2"/></>,
    layers: <><path d="m12 3 10 6-10 6L2 9l10-6Zm-9 11 9 5 9-5M3 19l9 5 9-5"/></>,
    pulse: <path d="M2 12h5l3-8 4 16 3-8h5"/>,
    check: <path d="m5 12 4 4L19 6"/>,
    close: <path d="m6 6 12 12M6 18 18 6"/>,
    info: <><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/></>,
    expand: <><path d="M8 3H3v5M16 3h5v5M21 16v5h-5M3 16v5h5"/></>,
    shield: <><path d="m12 3 8 3v7c0 5-8 9-8 9s-8-4-8-9V6l8-3Z"/><path d="m8 12 3 3 5-6"/></>,
  };
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" className={className} style={style} aria-hidden="true">{paths[name] ?? paths.grid}</svg>;
}
