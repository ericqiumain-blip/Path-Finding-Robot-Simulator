# Demo media

This directory is the intended home for media captured from the running simulator.

![Running Pathfinder dashboard](screenshot.png)

- `screenshot.png`: actual dashboard screenshot captured by the browser integration check.
- `demo.gif` or `demo.mp4`: optional future recording; these are not fabricated or bundled placeholders.

To record a demonstration:

1. Follow the root README to build the engine and dashboard, then run `npm start`.
2. Open `http://127.0.0.1:8080` at a desktop resolution.
3. Use the medium layout with 32 robots, seed 42 and 10× speed.
4. Show robots retrieving items, delivering to packing, and battery levels changing.
5. Select a robot, toggle planned paths, then switch to the congestion heatmap or reservations.
6. Export metrics to show that the visual demo and benchmark data come from the same engine.

A screenshot documents one observed state, not a claim about throughput capacity. Benchmark tables come only from native CLI runs.
