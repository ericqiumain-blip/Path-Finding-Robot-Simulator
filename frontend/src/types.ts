export type Point = { x: number; y: number };
export type Config = {
  robots: number;
  orderRate: number;
  scheduler: string;
  seed: number;
  layout: string;
  failuresEnabled?: boolean;
  failureProbability?: number;
};
export type Robot = Point & {
  id: number;
  state: string;
  battery: number;
  taskId: number | null;
  path: Point[];
  distance: number;
  replans: number;
  idleTicks?: number;
  chargingTicks?: number;
};
export type Order = {
  id: number;
  shelfId?: number;
  priority: number;
  createdTick: number;
  assignedRobot: number | null;
  status: 'pending' | 'assigned' | 'completed';
  completedTick?: number;
};
export type Metrics = {
  ordersGenerated: number;
  ordersCompleted: number;
  ordersPerHour: number;
  avgFulfillment: number;
  medianFulfillment: number;
  p95Fulfillment: number;
  utilization: number;
  totalDistance: number;
  collisionsPrevented: number;
  replans: number;
  avgPathfindingMs: number;
  congestionEvents: number;
  failedRobots: number;
  outstandingOrders: number;
};
export type HistoryPoint = { tick: number; ordersPerHour: number; completed?: number };
export type SimulationState = {
  tick: number;
  paused: boolean;
  speed: number;
  config: Config;
  warehouse: { width: number; height: number; cells: string[]; name?: string };
  robots: Robot[];
  orders: Order[];
  metrics: Metrics;
  heatmap: number[];
  reservations: (Point & { tick: number; robotId: number })[];
  history: HistoryPoint[];
};
export type Control = {
  action: 'pause' | 'resume' | 'step' | 'speed' | 'restart';
  speed?: number;
  config?: Config;
};
export type Overlay = 'paths' | 'heatmap' | 'reservations' | 'states';

export function stateLabel(state: string): string {
  return state.toLowerCase().replaceAll('_', ' ').replace(/^./, s => s.toUpperCase());
}
export function robotCategory(state: string): 'working' | 'idle' | 'waiting' | 'charging' | 'failed' {
  if (state === 'IDLE') return 'idle';
  if (state === 'WAITING') return 'waiting';
  if (state === 'FAILED') return 'failed';
  if (state === 'CHARGING' || state === 'MOVING_TO_CHARGE') return 'charging';
  return 'working';
}
export const robotColors = { working: '#70e7ce', idle: '#8393ad', waiting: '#f1bc66', charging: '#ae9bfa', failed: '#ed7889' };
export const robotName = (id: number) => `R-${String(id).padStart(3, '0')}`;
