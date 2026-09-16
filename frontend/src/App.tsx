import { useEffect, useMemo, useState } from 'react';
import { WarehouseCanvas } from './components/WarehouseCanvas';
import { FleetDistribution, ThroughputChart, duration, number } from './components/Charts';
import { Icon } from './components/Icon';
import { useSimulation } from './useSimulation';
import { robotCategory, robotColors, robotName, stateLabel } from './types';
import type { Config, Overlay, Robot } from './types';

const initialConfig: Config = { robots: 32, orderRate: 1.5, scheduler: 'nearest', seed: 42, layout: 'medium', failuresEnabled: false, failureProbability: .00005 };
const schedulers: Record<string, string> = { random: 'Random assignment', nearest: 'Nearest robot', cost: 'Route cost', hungarian: 'Hungarian batch' };
const layoutNames: Record<string, string> = { small: 'Small warehouse', medium: 'Medium warehouse', large: 'Large warehouse', congested: 'Congestion test', procedural: 'Procedural warehouse' };
const overlayNames: { key: Overlay; name: string; icon: string }[] = [{ key: 'states', name: 'Robot states', icon: 'bot' }, { key: 'paths', name: 'Planned paths', icon: 'route' }, { key: 'heatmap', name: 'Heatmap', icon: 'pulse' }, { key: 'reservations', name: 'Reservations', icon: 'layers' }];

function RobotBadge({ robot }: { robot: Robot }) {
  const color = robotColors[robotCategory(robot.state)];
  return <span className="state-badge" style={{ color, background: color + '14' }}><i style={{ background: color }} />{stateLabel(robot.state)}</span>;
}

export default function App() {
  const { state, connected, busy, error, control, clearError } = useSimulation();
  const [draft, setDraft] = useState<Config>(initialConfig);
  const [configured, setConfigured] = useState(false);
  const [view, setView] = useState<'live' | 'fleet' | 'analytics'>('live');
  const [selected, setSelected] = useState<number | null>(0);
  const [overlays, setOverlays] = useState<Set<Overlay>>(new Set(['states']));
  const [search, setSearch] = useState('');
  const [expanded, setExpanded] = useState(false);
  useEffect(() => {
    if (state && !configured) {
      const { robots, orderRate, scheduler, seed, layout, failuresEnabled, failureProbability } = state.config;
      setDraft({ robots, orderRate, scheduler, seed, layout, failuresEnabled: failuresEnabled ?? false, failureProbability: failureProbability ?? .00005 });
      setConfigured(true);
    }
    if (state && !state.robots.some(r => r.id === selected)) setSelected(state.robots[0]?.id ?? null);
  }, [state, configured, selected]);
  const robot = state?.robots.find(r => r.id === selected);
  const metrics = state?.metrics;
  const working = state?.robots.filter(r => robotCategory(r.state) === 'working').length ?? 0;
  const tasks = useMemo(() => state?.orders.filter(o => o.status !== 'completed').slice(0, 6) ?? [], [state]);
  const dirty = state && Object.entries(draft).some(([key, value]) => state.config[key as keyof Config] !== value);
  const invalid = !Number.isInteger(draft.robots) || draft.robots < 1 || draft.robots > 1000 || !Number.isFinite(draft.orderRate) || draft.orderRate < 0 || draft.orderRate > 100 || !Number.isInteger(draft.seed) || draft.seed < 0 || draft.seed > 4294967295;
  const change = <K extends keyof Config>(key: K, value: Config[K]) => setDraft(previous => ({ ...previous, [key]: value }));
  const toggle = (key: Overlay) => setOverlays(previous => { const next = new Set(previous); if (next.has(key)) next.delete(key); else next.add(key); return next; });
  const choose = (id: number) => { setSelected(id); setView('live'); };
  const restart = async () => { await control({ action: 'restart', config: draft }); };

  return <div className="app-shell">
    <aside className="sidebar">
      <a className="brand" href="#" onClick={event => { event.preventDefault(); setView('live'); }} aria-label="Pathfinder home"><span className="brand-mark"><Icon name="route" size={23}/></span><span>pathfinder<span className="brand-caption">WAREHOUSE AUTONOMY LAB</span></span></a>
      <div className="workspace-tag"><span className="workspace-icon">W</span><span>Warehouse simulation<small>Local workspace</small></span><span className="version">v1.0</span></div>
      <span className="sidebar-label">OBSERVE</span>
      <nav aria-label="Dashboard sections">
        {([{ key: 'live', label: 'Live floor', icon: 'grid' }, { key: 'fleet', label: 'Robot fleet', icon: 'bot' }, { key: 'analytics', label: 'Analytics', icon: 'chart' }] as const).map(item => <button key={item.key} className={`nav-item ${view === item.key ? 'active' : ''}`} aria-current={view === item.key ? 'page' : undefined} onClick={() => setView(item.key)}><Icon name={item.icon}/>{item.label}{item.key === 'live' ? <i className="live-dot"/> : item.key === 'fleet' ? <span className="nav-count">{state?.robots.length ?? '—'}</span> : null}</button>)}
      </nav>
      <div className="sidebar-divider"/>
      <div className="config-heading"><span className="sidebar-label">SCENARIO</span><Icon name="sliders" size={15}/></div>
      <form className="config-form" onSubmit={event => { event.preventDefault(); void restart(); }}>
        <label>Warehouse layout<select value={draft.layout} onChange={event => change('layout', event.target.value)}>{Object.entries(layoutNames).map(([key, name]) => <option key={key} value={key}>{name}</option>)}</select></label>
        <label>Assignment strategy<select value={draft.scheduler} onChange={event => change('scheduler', event.target.value)}>{Object.entries(schedulers).map(([key, name]) => <option key={key} value={key}>{name}</option>)}</select></label>
        <label>Fleet size<span className="input-unit"><input aria-label="Robot count" type="number" min="1" max="1000" value={draft.robots} onChange={event => change('robots', Number(event.target.value))}/><span>robots</span></span></label>
        <input className="fleet-range" aria-label="Adjust robot count" type="range" min="1" max="1000" value={draft.robots} onChange={event => change('robots', Number(event.target.value))}/>
        <label>Order arrival rate<span className="input-unit"><input aria-label="Order arrival rate" type="number" min="0" max="100" step="0.1" value={draft.orderRate} onChange={event => change('orderRate', Number(event.target.value))}/><span>/ sec</span></span></label>
        <label>Random seed<span className="input-unit"><input aria-label="Random seed" type="number" min="0" max="4294967295" value={draft.seed} onChange={event => change('seed', Number(event.target.value))}/><Icon name="reset" size={14}/></span></label>
        <label className="check-label"><input type="checkbox" checked={draft.failuresEnabled ?? false} onChange={event => change('failuresEnabled', event.target.checked)}/>Random failures</label>
        {draft.failuresEnabled && <label>Failure chance / tick<input aria-label="Failure probability" type="number" min="0" max="1" step="0.0001" value={draft.failureProbability ?? .00005} onChange={event => change('failureProbability', Number(event.target.value))}/></label>}
        <button className="apply-button" disabled={!connected || busy || invalid} type="submit"><Icon name="reset" size={15}/>{dirty ? 'Apply & restart' : 'Restart scenario'}</button>
        <p className="form-hint">{dirty ? 'Changes apply to a new simulation.' : 'A fixed seed makes every run reproducible.'}</p>
      </form>
      <div className="sidebar-bottom"><span className="engine-icon"><Icon name="pulse" size={18}/></span><div>C++ simulation engine<small>{connected ? 'Connected · deterministic ticks' : 'Waiting for connection'}</small></div><i className={`connection-dot ${connected ? 'on' : ''}`}/></div>
    </aside>

    <div className="main-shell">
      <header className="topbar"><div className="breadcrumb">WORKSPACE <span>/</span> <strong>FULFILLMENT CENTER</strong></div><div className="topbar-right"><span className={`connection-label ${connected ? 'online' : ''}`}><i className="connection-dot"/>{connected ? 'Engine connected' : 'Connecting to engine'}</span><span className="local-tag">LOCAL</span></div></header>
      <main>
        <section className="page-heading"><div><div className="eyebrow">AUTONOMOUS OPERATIONS</div><h1>{view === 'live' ? 'Warehouse overview' : view === 'fleet' ? 'Robot fleet' : 'Performance analytics'}<span className="heading-dot">.</span></h1><p>{view === 'live' ? 'Warehouse status and simulation controls.' : view === 'fleet' ? 'Inspect every robot and the work it is doing.' : 'Metrics for the current simulation.'}</p></div><div className="export-group"><a className="secondary-button" href="/api/metrics?format=csv" download><Icon name="download" size={16}/>Export CSV</a><a className="icon-button" href="/api/metrics?format=json" download aria-label="Export metrics as JSON" title="Export JSON"><Icon name="box" size={17}/></a></div></section>
        {error && <div className="error-banner" role="alert"><Icon name="info"/>{error}<button className="icon-button" aria-label="Dismiss error" onClick={clearError}><Icon name="close"/></button></div>}
        {!state ? <section className="connecting-panel"><span className="connecting-mark"><Icon name="bot" size={40}/></span><h2>Connecting to the warehouse</h2><p>Your live floor will appear when the simulation engine is ready.</p><code>npm start</code><small>Run the command from the project folder, then keep this page open.</small></section> : <>
          {!connected && <div className="error-banner" role="status"><Icon name="info"/>Connection interrupted. Showing the last received state while reconnecting.</div>}
          <section className="metrics-grid" aria-label="Live simulation metrics">
            <div className="metric-card"><div className="metric-label">Fleet online<Icon name="bot"/></div><div className="metric-value">{number(state.robots.length - metrics!.failedRobots)}<span>/ {state.robots.length}</span></div><div className="metric-detail"><span className="mini-dot mint"/>{working} robots working<span className="metric-note">LIVE</span></div></div>
            <div className="metric-card"><div className="metric-label">Orders fulfilled<Icon name="box"/></div><div className="metric-value" data-testid="orders-completed">{number(metrics!.ordersCompleted)}<span>orders</span></div><div className="metric-detail"><span className="mini-dot amber"/>{number(metrics!.outstandingOrders)} outstanding</div></div>
            <div className="metric-card"><div className="metric-label">Warehouse throughput<Icon name="chart"/></div><div className="metric-value">{number(metrics!.ordersPerHour)}<span>/ hour</span></div><div className="metric-detail">Average since simulation start</div></div>
            <div className="metric-card"><div className="metric-label">Robot utilization<Icon name="pulse"/></div><div className="metric-value">{number(metrics!.utilization * 100, 1)}<span>%</span></div><div className="utilization-track"><span style={{ width: `${metrics!.utilization * 100}%` }}/></div><div className="metric-detail">Time assigned to an order</div></div>
          </section>

          <div className="playback-bar"><div className="run-state"><span className={`live-dot ${state.paused ? 'paused' : ''}`}/><strong>{state.paused ? 'Simulation paused' : 'Simulation running'}</strong><span className="run-divider"/><span className="sim-clock" data-testid="simulation-clock">{duration(state.tick)}</span><span className="tick-counter">TICK {number(state.tick)}</span></div><div className="playback-controls"><div className="speed-control" aria-label="Simulation speed">{[1, 5, 10, 50].map(speed => <button aria-pressed={state.speed === speed} key={speed} onClick={() => void control({ action: 'speed', speed })} disabled={busy || !connected}>{speed}×</button>)}</div><button className="icon-button step-button" aria-label="Advance one tick" title="Advance one tick" onClick={() => void control({ action: 'step' })} disabled={busy || !connected}><Icon name="step" size={17}/></button><button className="play-button" onClick={() => void control({ action: state.paused ? 'resume' : 'pause' })} disabled={busy || !connected}><Icon name={state.paused ? 'play' : 'pause'} size={15}/>{state.paused ? 'Resume' : 'Pause'}</button></div></div>

          {view === 'live' && <div className={`operations-grid ${expanded ? 'expanded' : ''}`}>
            <div className="floor-column"><section className="panel floor-panel"><div className="panel-heading"><div><span className="section-kicker">LIVE FLOOR</span><h2>{layoutNames[state.config.layout] ?? state.config.layout}<span className="dimension-label">{state.warehouse.width} × {state.warehouse.height}</span></h2></div><button className="icon-button" aria-label={expanded ? 'Restore floor size' : 'Expand floor'} onClick={() => setExpanded(!expanded)} title="Expand floor"><Icon name="expand" size={17}/></button></div><div className="overlay-bar" aria-label="Map overlays">{overlayNames.map(overlay => <button key={overlay.key} aria-pressed={overlays.has(overlay.key)} onClick={() => toggle(overlay.key)}><Icon name={overlay.icon} size={14}/>{overlay.name}</button>)}</div><div className="canvas-container"><WarehouseCanvas state={state} overlays={overlays} selected={selected} onSelect={setSelected}/><span className="map-caption"><span className="mini-dot mint"/> LIVE TELEMETRY <span>1 CELL = 1 M</span></span></div><div className="map-legend"><span><i className="legend-shelf"/>Shelving</span><span><i className="legend-pack"/>Packing</span><span><i className="legend-charge"/>Charging</span><span><i className="legend-robot"/>Robot</span><small>Select a robot to trace its route</small></div></section>
              <section className="panel throughput-panel"><div className="panel-heading"><div><h2>Throughput over time</h2><p>Completed orders per hour · cumulative average</p></div><span className="chart-label"><span className="line-key"/>Orders / hr</span></div><ThroughputChart history={state.history ?? []}/></section>
            </div>
            <aside className="detail-column"><section className="panel inspector"><div className="panel-heading"><h2>Robot inspector</h2><Icon name="bot" size={17}/></div><label className="sr-only" htmlFor="robot-selector">Select robot</label><select id="robot-selector" className="robot-selector" value={selected ?? ''} onChange={event => setSelected(Number(event.target.value))}>{state.robots.map(r => <option key={r.id} value={r.id}>{robotName(r.id)} · {stateLabel(r.state)}</option>)}</select>{robot && <><div className="robot-profile"><span className="robot-avatar"><Icon name="bot" size={34}/></span><div><h3 data-testid="selected-robot">{robotName(robot.id)}</h3><RobotBadge robot={robot}/></div></div><div className="battery-heading"><span><Icon name="bolt" size={13}/>Battery level</span><strong>{number(robot.battery, 1)}<small>%</small></strong></div><div className={`battery-track ${robot.battery < 25 ? 'low' : ''}`}><span style={{ width: `${robot.battery}%` }}/></div><dl className="robot-facts"><div><dt>Current task</dt><dd>{robot.taskId === null ? 'Unassigned' : `Order #${robot.taskId}`}</dd></div><div><dt>Position</dt><dd>({robot.x}, {robot.y})</dd></div><div><dt>Distance traveled</dt><dd>{number(robot.distance)} <span>m</span></dd></div><div><dt>Remaining path</dt><dd>{robot.path.length} <span>cells</span></dd></div><div><dt>Route replans</dt><dd>{robot.replans}</dd></div></dl><div className="inspector-note"><Icon name="route" size={14}/>{robot.path.length ? 'Selected route highlighted on the floor.' : 'Ready for the next route update.'}</div></>}</section>
              <section className="panel fleet-panel"><div className="panel-heading"><h2>Fleet activity</h2><span className="subtle-tag">{state.robots.length} robots</span></div><FleetDistribution robots={state.robots}/></section>
              <section className="panel queue-panel"><div className="panel-heading"><h2>Order queue</h2><span className="queue-count">{number(metrics!.outstandingOrders)}</span></div><div className="order-list">{tasks.length ? tasks.map(order => <div className="order-row" key={order.id}><span className="order-icon"><Icon name="box" size={15}/></span><div><strong>Order #{String(order.id).padStart(4, '0')}</strong><small>{order.assignedRobot !== null ? `Assigned to ${robotName(order.assignedRobot)}` : `Awaiting assignment · shelf ${order.shelfId}`}</small></div><span className={`priority p${order.priority}`}>{order.priority >= 3 ? 'HIGH' : order.status === 'assigned' ? 'ACTIVE' : 'QUEUED'}</span></div>) : <p className="empty-queue">No outstanding orders. The floor is clear.</p>}</div></section>
            </aside>
          </div>}

          {view === 'fleet' && <section className="panel fleet-table-panel"><div className="panel-heading"><div><h2>Fleet directory</h2><p>Select a robot to inspect its route on the floor.</p></div><input className="search-input" aria-label="Search robots" placeholder="Search robot ID…" value={search} onChange={event => setSearch(event.target.value)}/></div><div className="table-scroll"><table><thead><tr><th>Robot</th><th>State</th><th>Battery</th><th>Task</th><th>Distance</th><th>Replans</th><th/></tr></thead><tbody>{state.robots.filter(r => robotName(r.id).toLowerCase().includes(search.toLowerCase())).map(r => <tr key={r.id}><td><button className="robot-link" onClick={() => choose(r.id)}><Icon name="bot" size={17}/>{robotName(r.id)}</button></td><td><RobotBadge robot={r}/></td><td><span className="table-battery">{number(r.battery, 1)}%<i style={{ width: `${r.battery * .5}px`, background: r.battery < 25 ? '#efbc72' : '#79dcc0' }}/></span></td><td>{r.taskId === null ? '—' : `#${r.taskId}`}</td><td>{number(r.distance)} m</td><td>{r.replans}</td><td><button className="icon-button" aria-label={`Inspect ${robotName(r.id)}`} onClick={() => choose(r.id)}><Icon name="arrow" size={15}/></button></td></tr>)}</tbody></table></div></section>}

          {view === 'analytics' && <div className="analytics-grid"><section className="panel analytics-chart"><div className="panel-heading"><div><h2>Warehouse throughput</h2><p>Live observations from this run</p></div><span className="chart-label"><span className="line-key"/>Orders / hr</span></div><ThroughputChart history={state.history ?? []} large/></section><section className="panel analytics-fleet"><div className="panel-heading"><h2>Current fleet distribution</h2></div><FleetDistribution robots={state.robots}/></section><section className="panel metric-table"><div className="panel-heading"><h2>Run measurements</h2><span className="subtle-tag">Seed {state.config.seed}</span></div><dl>{([
            ['Orders generated', number(metrics!.ordersGenerated)], ['Orders completed', number(metrics!.ordersCompleted)], ['Outstanding orders', number(metrics!.outstandingOrders)], ['Average fulfillment', `${number(metrics!.avgFulfillment, 1)} s`], ['Median fulfillment', `${number(metrics!.medianFulfillment, 1)} s`], ['95th percentile fulfillment', `${number(metrics!.p95Fulfillment, 1)} s`], ['Total distance', `${number(metrics!.totalDistance)} m`], ['Collision interventions', number(metrics!.collisionsPrevented)], ['Route replans', number(metrics!.replans)], ['Average A* search time', `${number(metrics!.avgPathfindingMs, 3)} ms`], ['Congestion events', number(metrics!.congestionEvents)], ['Failed robots', number(metrics!.failedRobots)],
          ]).map(([label, value]) => <div key={label}><dt>{label}</dt><dd>{value}</dd></div>)}</dl><p className="analytics-note"><Icon name="info" size={15}/>Fulfillment statistics include completed orders only. Utilization includes time waiting on assigned tasks.</p></section></div>}
          <footer className="page-footer"><span><Icon name="shield" size={13}/>Atomic movement · cell & edge reservations</span><span>SEED {state.config.seed}<i/> {schedulers[state.config.scheduler]?.toUpperCase() ?? state.config.scheduler} <i/> {state.paused ? 'PAUSED' : 'LIVE'}</span></footer>
        </>}
      </main>
    </div>
  </div>;
}
