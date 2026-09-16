# Local API

Start with `npm start` after both builds. The bridge serves the dashboard and API at `http://127.0.0.1:8080`. `PORT` changes the port; `WAREHOUSE_EXECUTABLE` selects another compiled binary. The server binds to loopback.

| Method | Path | Behavior |
| --- | --- | --- |
| GET | `/api/health` | Child process availability and readiness |
| GET | `/api/state` | Current snapshot with config, topology, robots, orders, metrics, reservations and history |
| GET | `/api/events` | SSE stream; each `event: state` carries a JSON snapshot |
| POST | `/api/control` | Pause, resume, step, speed, or restart |
| GET | `/api/metrics?format=json` | Download full metrics and configuration |
| GET | `/api/metrics?format=csv` | Download a flat metrics row |

Examples:

```json
{"action":"pause"}
{"action":"resume"}
{"action":"step"}
{"action":"speed","speed":50}
{"action":"restart","config":{"robots":100,"orderRate":2.5,"scheduler":"hungarian","seed":42,"layout":"large"}}
```

Speed is one of `1`, `5`, `10`, `50`. A single step leaves the simulation paused. Restart merges the supplied configuration with the existing configuration and resets the engine; pause and speed preferences remain. Robot count is 1–1000 through the API; arrival rate is 0–100 orders/s. Layout is `small`, `medium`, `large`, `congested`, or `procedural`. The selected fixed map must have room for the requested fleet. `failuresEnabled` and `failureProbability` can also be passed on restart.

The snapshot's `orders` array is capped at 100 records, preferring active orders then recent completions. `metrics` always counts the complete workload. `battery` is a percentage, `path` contains remaining cells, and `utilization` is a fraction in `[0,1]`. The history is a bounded series of cumulative orders/hour samples. Simulation statistics use simulated seconds; `avgPathfindingMs` uses wall-clock milliseconds.

## Direct C++ stream

`warehouse_sim --interactive` emits an initial snapshot then accepts one JSON object per input line:

```json
{"action":"snapshot"}
{"action":"step","ticks":10}
{"action":"restart","config":{"robots":25,"scheduler":"cost"}}
{"action":"fail","robotId":0,"duration":30,"permanent":false}
```

Each request receives one snapshot or `{"error":"..."}`. Step accepts 1–1000 ticks. EOF cleanly terminates the process. The bridge keeps commands serialized, so responses cannot be assigned to the wrong requester. Stdout contains protocol data; diagnostics use stderr.
