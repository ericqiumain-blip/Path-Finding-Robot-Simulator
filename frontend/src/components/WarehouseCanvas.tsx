import { useEffect, useRef } from 'react';
import type { Overlay, Robot, SimulationState } from '../types';
import { robotCategory, robotColors, robotName } from '../types';

type Props = {
  state: SimulationState;
  overlays: Set<Overlay>;
  selected: number | null;
  onSelect: (id: number) => void;
};

export function WarehouseCanvas({ state, overlays, selected, onSelect }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const view = useRef({ cell: 1, left: 0, top: 0 });
  const current = useRef(state);
  const prior = useRef<Map<number, { x: number; y: number }>>(new Map());
  const updated = useRef(performance.now());
  const frameGap = useRef(150);

  useEffect(() => {
    const now = performance.now();
    prior.current = new Map(current.current.robots.map(robot => [robot.id, { x: robot.x, y: robot.y }]));
    frameGap.current = Math.min(350, Math.max(40, now - updated.current));
    updated.current = now;
    current.current = state;
  }, [state]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas?.getContext('2d');
    if (!canvas || !context) return;
    let animation = 0;
    let width = 0;
    let height = 0;
    const resize = new ResizeObserver(entries => {
      const rect = entries[0].contentRect;
      width = rect.width;
      height = rect.height;
      const ratio = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.round(width * ratio);
      canvas.height = Math.round(height * ratio);
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
    });
    resize.observe(canvas);
    function draw(now: number) {
      if (!context) return;
      const { warehouse, robots, heatmap, reservations, tick } = current.current;
      if (width < 1 || height < 1) { animation = requestAnimationFrame(draw); return; }
      const cell = Math.min((width - 50) / warehouse.width, (height - 48) / warehouse.height);
      const left = (width - cell * warehouse.width) / 2;
      const top = (height - cell * warehouse.height) / 2;
      view.current = { cell, left, top };
      const xAt = (x: number) => left + (x + .5) * cell;
      const yAt = (y: number) => top + (y + .5) * cell;
      context.clearRect(0, 0, width, height);
      context.fillStyle = '#101a26';
      context.fillRect(left, top, cell * warehouse.width, cell * warehouse.height);
      context.font = '9px "SFMono-Regular", Consolas, monospace';
      context.textAlign = 'center';
      context.textBaseline = 'middle';
      for (let y = 0; y < warehouse.height; y++) {
        if (y % 5 === 0) {
          context.fillStyle = '#4f6379';
          context.fillText(String(y).padStart(2, '0'), left - 14, yAt(y));
        }
        for (let x = 0; x < warehouse.width; x++) {
          const cx = left + x * cell;
          const cy = top + y * cell;
          const type = warehouse.cells[y]?.[x] ?? '.';
          context.strokeStyle = '#1a2837';
          context.lineWidth = .5;
          context.strokeRect(cx, cy, cell, cell);
          if (type === '#') {
            context.fillStyle = '#213042';
            context.fillRect(cx + .5, cy + .5, cell - 1, cell - 1);
            context.fillStyle = '#2a3d50';
            context.fillRect(cx + 1, cy + 1, cell - 2, 1);
          } else if (type === 'S') {
            const inset = Math.max(1.5, cell * .12);
            context.fillStyle = '#2b3d50';
            context.fillRect(cx + inset, cy + inset, cell - inset * 2, cell - inset * 2);
            context.fillStyle = '#3d5369';
            context.fillRect(cx + inset, cy + inset, cell - inset * 2, Math.max(1, cell * .1));
            if (cell > 10) {
              context.strokeStyle = '#1b2b3d';
              context.lineWidth = 1;
              context.beginPath();
              context.moveTo(cx + cell * .5, cy + inset + 2);
              context.lineTo(cx + cell * .5, cy + cell - inset);
              context.stroke();
            }
          } else if (type === 'P' || type === 'C') {
            context.fillStyle = type === 'P' ? '#214239' : '#343047';
            context.fillRect(cx + 1, cy + 1, cell - 2, cell - 2);
            context.strokeStyle = type === 'P' ? '#528e78' : '#726295';
            context.lineWidth = 1;
            context.strokeRect(cx + 1.5, cy + 1.5, cell - 3, cell - 3);
            if (cell > 12) {
              context.fillStyle = type === 'P' ? '#95cfac' : '#baaae8';
              context.font = `600 ${Math.min(10, cell * .5)}px system-ui`;
              context.fillText(type === 'P' ? 'P' : '↯', xAt(x), yAt(y));
            }
          }
        }
      }
      context.font = '9px "SFMono-Regular", Consolas, monospace';
      context.fillStyle = '#4f6379';
      for (let x = 0; x < warehouse.width; x += 5) context.fillText(String(x).padStart(2, '0'), xAt(x), top - 12);
      if (overlays.has('heatmap') && heatmap?.length) {
        const max = Math.max(1, ...heatmap);
        for (let i = 0; i < heatmap.length; i++) {
          if (!heatmap[i]) continue;
          const value = Math.sqrt(heatmap[i] / max);
          context.fillStyle = `rgba(245, ${Math.round(160 - value * 80)}, 66, ${value * .67})`;
          context.fillRect(left + i % warehouse.width * cell, top + Math.floor(i / warehouse.width) * cell, cell, cell);
        }
      }
      if (overlays.has('reservations')) {
        for (const reservation of reservations ?? []) {
          if (reservation.tick < tick || reservation.tick > tick + 12) continue;
          context.fillStyle = `rgba(181, 158, 252, ${Math.max(.08, .4 - (reservation.tick - tick) * .025)})`;
          context.fillRect(left + reservation.x * cell + cell * .15, top + reservation.y * cell + cell * .15, cell * .7, cell * .7);
        }
      }
      const pathRobots = overlays.has('paths') ? robots : robots.filter(robot => robot.id === selected);
      for (const robot of pathRobots) {
        if (!robot.path?.length) continue;
        context.beginPath();
        context.moveTo(xAt(robot.x), yAt(robot.y));
        for (const point of robot.path) context.lineTo(xAt(point.x), yAt(point.y));
        context.strokeStyle = robot.id === selected ? '#85efd7' : '#51bda25c';
        context.lineWidth = robot.id === selected ? 1.8 : 1;
        context.setLineDash(robot.id === selected ? [4, 4] : [2, 5]);
        context.stroke();
        context.setLineDash([]);
        if (robot.id === selected) {
          const destination = robot.path[robot.path.length - 1];
          context.strokeStyle = '#85efd7';
          context.lineWidth = 1.5;
          context.strokeRect(left + destination.x * cell + 2, top + destination.y * cell + 2, cell - 4, cell - 4);
        }
      }
      const progress = Math.min(1, (now - updated.current) / frameGap.current);
      const sorted = [...robots].sort((a, b) => Number(a.id === selected) - Number(b.id === selected));
      for (const robot of sorted) {
        const previous = prior.current.get(robot.id) ?? robot;
        const adjacent = Math.abs(previous.x - robot.x) + Math.abs(previous.y - robot.y) <= 2;
        const x = adjacent ? previous.x + (robot.x - previous.x) * progress : robot.x;
        const y = adjacent ? previous.y + (robot.y - previous.y) * progress : robot.y;
        const cx = xAt(x);
        const cy = yAt(y);
        const radius = Math.max(2.1, cell * .28);
        const color = overlays.has('states') ? robotColors[robotCategory(robot.state)] : '#70e7ce';
        if (robot.id === selected) {
          context.beginPath();
          context.arc(cx, cy, radius + 5, 0, Math.PI * 2);
          context.fillStyle = '#79e7d022';
          context.fill();
          context.strokeStyle = '#a4f7e7';
          context.lineWidth = 1;
          context.stroke();
        }
        context.shadowColor = color;
        context.shadowBlur = cell > 8 ? 6 : 0;
        context.beginPath();
        context.arc(cx, cy, radius, 0, Math.PI * 2);
        context.fillStyle = color;
        context.fill();
        context.shadowBlur = 0;
        context.strokeStyle = '#0b1722';
        context.lineWidth = 1;
        context.stroke();
        if (cell > 10) {
          context.fillStyle = '#193e3c';
          context.fillRect(cx - radius * .45, cy - radius * .4, radius * .9, radius * .8);
        }
        if (robot.id === selected) {
          const labelWidth = 44;
          const labelY = cy - radius - 22;
          context.fillStyle = '#d9fff5';
          context.beginPath();
          context.roundRect(cx - labelWidth / 2, labelY, labelWidth, 16, 4);
          context.fill();
          context.fillStyle = '#153b34';
          context.font = 'bold 9px Consolas, monospace';
          context.fillText(robotName(robot.id), cx, labelY + 8);
        }
      }
      animation = requestAnimationFrame(draw);
    }
    animation = requestAnimationFrame(draw);
    return () => { resize.disconnect(); cancelAnimationFrame(animation); };
  }, [overlays, selected]);

  const select = (event: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const { cell, left, top } = view.current;
    const x = (event.clientX - rect.left - left) / cell - .5;
    const y = (event.clientY - rect.top - top) / cell - .5;
    const nearest = state.robots.reduce<{ robot: Robot | null; distance: number }>((best, robot) => {
      const distance = Math.hypot(robot.x - x, robot.y - y);
      return distance < best.distance ? { robot, distance } : best;
    }, { robot: null, distance: 1 });
    if (nearest.robot) onSelect(nearest.robot.id);
  };

  return <canvas ref={canvasRef} onClick={select} className="warehouse-canvas" role="img" aria-label={`Warehouse map, ${state.warehouse.width} by ${state.warehouse.height} cells, with ${state.robots.length} robots. Use the robot selector to inspect a robot with the keyboard.`} />;
}
