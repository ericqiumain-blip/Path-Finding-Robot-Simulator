import type { HistoryPoint, Robot } from '../types';
import { robotCategory, robotColors } from '../types';

export const number = (value: number | undefined, digits = 0) => value === undefined || !Number.isFinite(value) ? '—' : value.toLocaleString('en-US', { maximumFractionDigits: digits });
export const duration = (tick: number) => `${Math.floor(tick / 3600).toString().padStart(2, '0')}:${Math.floor(tick / 60 % 60).toString().padStart(2, '0')}:${Math.floor(tick % 60).toString().padStart(2, '0')}`;

export function ThroughputChart({ history, large = false }: { history: HistoryPoint[]; large?: boolean }) {
  const points = history.slice(-90);
  const width = 680;
  const height = large ? 240 : 118;
  const top = 12;
  const bottom = height - 24;
  const max = Math.max(1, ...points.map(point => point.ordersPerHour)) * 1.15;
  const start = points[0]?.tick ?? 0;
  const end = points[points.length - 1]?.tick ?? 1;
  const coords = points.map(point => ({ x: 38 + (point.tick - start) / Math.max(1, end - start) * (width - 50), y: bottom - point.ordersPerHour / max * (bottom - top) }));
  const line = coords.map((point, index) => `${index ? 'L' : 'M'}${point.x.toFixed(2)},${point.y.toFixed(2)}`).join(' ');
  const area = coords.length ? `${line} L${coords[coords.length - 1].x},${bottom} L${coords[0].x},${bottom} Z` : '';
  return <div className={`chart-wrap ${large ? 'large-chart' : ''}`}>
    {points.length < 2 ? <div className="chart-empty">Throughput history appears as the simulation advances.</div> :
      <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label={`Orders per hour from tick ${start} to ${end}. Current throughput ${number(points[points.length - 1].ordersPerHour)} orders per hour.`}>
        <defs><linearGradient id={large ? 'chart-fill-large' : 'chart-fill'} x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stopColor="#72debf" stopOpacity=".22"/><stop offset="100%" stopColor="#72debf" stopOpacity="0"/></linearGradient></defs>
        {[0, .5, 1].map(fraction => <g key={fraction}><line x1="38" x2={width - 8} y1={bottom - fraction * (bottom - top)} y2={bottom - fraction * (bottom - top)} stroke="#263343" strokeDasharray="3 5"/><text x="28" y={bottom - fraction * (bottom - top) + 3} textAnchor="end" fill="#75879c" fontSize="9">{number(max * fraction)}</text></g>)}
        <path d={area} fill={`url(#${large ? 'chart-fill-large' : 'chart-fill'})`}/><path d={line} fill="none" stroke="#83e3c5" strokeWidth="2" vectorEffect="non-scaling-stroke"/>
        <circle cx={coords[coords.length - 1].x} cy={coords[coords.length - 1].y} r="3" fill="#b5f9df"/>
        {[0, .25, .5, .75, 1].map(fraction => <text key={fraction} x={38 + fraction * (width - 50)} y={height - 4} textAnchor={fraction === 0 ? 'start' : fraction === 1 ? 'end' : 'middle'} fill="#6e8095" fontSize="9">{duration(start + fraction * (end - start))}</text>)}
      </svg>}
  </div>;
}

export function FleetDistribution({ robots }: { robots: Robot[] }) {
  const counts = { working: 0, idle: 0, waiting: 0, charging: 0, failed: 0 };
  for (const robot of robots) counts[robotCategory(robot.state)]++;
  return <div className="fleet-distribution">
    <div className="distribution-bar" aria-label="Fleet state distribution">{Object.entries(counts).map(([name, count]) => <span key={name} style={{ width: `${count / Math.max(1, robots.length) * 100}%`, background: robotColors[name as keyof typeof counts] }} />)}</div>
    <div className="distribution-list">{Object.entries(counts).map(([name, count]) => <div className="distribution-item" key={name}><span className="dot" style={{ background: robotColors[name as keyof typeof counts] }}/><span>{name === 'working' ? 'Working' : name[0].toUpperCase() + name.slice(1)}</span><strong>{count}</strong></div>)}</div>
  </div>;
}
