# Benchmark methodology

Benchmarks invoke the Release C++ executable directly. They do not run the React frontend, HTTP server or snapshot serialization inside the measured tick loop. Each run records the full configuration, metrics, simulated ticks and wall time; no performance result is estimated or fabricated.

## Reproduce the fleet sweep

```sh
npm run build:engine
npm run benchmark
```

The default matrix is:

- Fleets: **10, 25, 50, 100, 250, 500, 1000**.
- Schedulers: **random, nearest, cost, hungarian**.
- Layout: the same checked-in `configs/large.map` (110 × 82 cells).
- Seed: **42**, shared across all experiments.
- Horizon: **300 ticks**, one simulated second each; no warm-up removal.
- Initial backlog: **100 orders**; Poisson arrival rate **10/s**; order cap **10000**.
- Battery management: enabled, default energy parameters; failures: disabled.
- Build: Release; one repetition per combination in the checked-in baseline.

This is a short scaling and congestion experiment. It is not a steady-state capacity claim. At large fleet sizes there is heavy competition for common pickup and packing cells. Results can become worse as more robots are added. Hungarian minimizes batch approach cost, so it is not guaranteed to produce higher throughput than a policy that happens to distribute robots more widely.

The workload RNG is independent of scheduling. With an identical seed and fixed layout, each policy sees the same generated orders and arrival timestamps. The first five simulated minutes do not generate the entire 10000-order cap; `ordersGenerated`, `ordersCompleted` and `outstandingOrders` in every raw JSON make that distinction explicit.

### Outputs

`benchmarks/results/` contains:

- `environment.json`: timestamp, hardware, OS, compiler, options and source fingerprint.
- `results.json` and `results.csv`: all 28 runs.
- `<robots>-<scheduler>-<repeat>.json`: individual raw results and exact command arguments.
- `SUMMARY.md`: a generated Markdown table from those raw runs.
- `workload-10000.json`: a separate complete-workload experiment described below.
- `sustained-25.json`: a shorter complete 1000-order workload with recharging enabled.

See [the measured table](../benchmarks/results/SUMMARY.md). These files are checked in intentionally. Send ad hoc experiments to `benchmarks/local/` to keep the baseline intact.

## Longer and repeated experiments

```sh
node scripts/benchmark.mjs --robots 25,100,500 --schedulers nearest,hungarian --ticks 3600 --repeats 3 --output benchmarks/local/long
node scripts/benchmark.mjs --robots 100 --seed 7 --ticks 3600 --output benchmarks/local/seed-7
```

For stable timing comparisons, close other heavy processes, use the same compiler/build, repeat runs and summarize median wall time. Change seeds explicitly to test workload sensitivity. With fixed seeds, repeated non-timing outcomes should match on the same toolchain. Cross-standard-library bit-for-bit reproducibility is not promised.

## Complete 10000-order experiment

Run from the repository root (append `.exe` to the executable on Windows):

```sh
build/release/warehouse_sim --robots 25 --orders 10000 --initial-orders 10000 --order-rate 0 --layout procedural --ticks 60000 --until-complete --seed 42 --json benchmarks/local/workload-10000.json
```

Create `benchmarks/local/` before exporting there. This workload starts with all 10000 orders at tick zero. It tests eventual completion, recharge cycles, large pending queues and long-running metric accumulation. The `--ticks` argument is a hard upper bound; check `completedWorkload` rather than assuming the queue drained. The checked-in run completed all 10000 orders at tick **33227** with no outstanding work. This is a different map/workload from the fleet sweep and is not directly comparable to its throughput values.

The shorter saved run uses the same flags with `--orders 1000 --initial-orders 1000 --ticks 10000`. It completed at tick **2996**.

## Reading the metrics

| Field | Meaning |
| --- | --- |
| `ordersPerHour` | Completed orders divided by simulated elapsed time, scaled to one hour |
| `avgFulfillment`, `medianFulfillment`, `p95Fulfillment` | Creation-to-completion seconds for completed orders only |
| `outstandingOrders` | Generated minus completed, including assigned and pending work |
| `utilization` | Fraction of robot ticks owning an active task, including waits on that task |
| `totalDistance` | All successful robot moves, one meter per cell, including parking and charging |
| `avgDistancePerOrder` | All fleet distance divided by completed orders |
| `collisionsPrevented` | Rejected movement proposals, not actual contact events |
| `replans` | Replanning attempts caused by unavailable or blocked routes |
| `avgPathfindingMs` | Mean wall time of measured A* calls; excludes cached BFS preprocessing |
| `wallSeconds` | Time inside the benchmark tick loop; excludes setup, map loading and JSON writing |
| `simulatedTicksPerSecond` | Tick count divided by the measured loop wall time |

A low fulfillment percentile can be misleading when many orders remain queued. Always report completion count and backlog alongside latency. Compare optimization outcomes independently from CPU execution time. The collision safety evidence comes from invariant tests, not a zero-valued collision counter.

## Profiling directions

Run a Release build with debug symbols and a native sampling profiler. Useful hot spots are A* expansions after congestion, route-cache size, repeated pending-order sorting, parking candidate scans, and the four-frame collision forecast. Record profiler output separately from throughput benchmarks. The architecture currently favors understandable correctness over parallelism or a fixed memory budget.
