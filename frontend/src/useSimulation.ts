import { useCallback, useEffect, useState } from 'react';
import type { Control, SimulationState } from './types';

export function useSimulation() {
  const [state, setState] = useState<SimulationState | null>(null);
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    const abort = new AbortController();
    let disposed = false;
    const receive = (next: SimulationState) => {
      if (disposed) return;
      setState(next);
      setConnected(true);
    };
    fetch('/api/state', { signal: abort.signal })
      .then(async response => {
        if (!response.ok) throw new Error('The simulation engine is unavailable.');
        receive(await response.json());
      })
      .catch(() => { if (!disposed) setConnected(false); });
    const events = new EventSource('/api/events');
    const onState = (event: MessageEvent) => {
      try { receive(JSON.parse(event.data)); }
      catch { setError('An invalid update was received from the engine.'); }
    };
    events.addEventListener('state', onState);
    events.onerror = () => { if (!disposed) setConnected(false); };
    return () => {
      disposed = true;
      abort.abort();
      events.removeEventListener('state', onState);
      events.close();
    };
  }, []);

  const control = useCallback(async (payload: Control): Promise<boolean> => {
    setBusy(true);
    setError('');
    try {
      const response = await fetch('/api/control', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || 'The command could not be applied.');
      if (result.warehouse) setState(result);
      else if (result.state?.warehouse) setState(result.state);
      return true;
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Unable to reach the simulation engine.');
      return false;
    } finally { setBusy(false); }
  }, []);

  return { state, connected, busy, error, control, clearError: () => setError('') };
}
