# Algorithms

All routing, assignment and collision algorithms are implemented in C++. Catch2 supplies test assertions and nlohmann/json supplies serialization only.

## A* routing

The grid has four orthogonal neighbors. Walls and shelves are impassable; a shelf is served from a traversable adjacent pickup cell. Floor, charging and packing cells are traversable. `AStarPlanner` uses a binary min-priority queue and dense arrays for best-known cost and predecessor. Stale queue entries are discarded. Routes exclude the start cell and include the goal; an already-reached destination is a successful empty route.

```text
g(next) = g(current) + 1 + congestion_weight × recent_density(next)
h(next) = ManhattanDistance(next, goal)
f(next) = g(next) + h(next)
```

Nonnegative penalties preserve the admissibility and consistency of Manhattan distance. The planner therefore returns an optimal route under the supplied static cost field. Failed robots are obstacles; occupancy-aware replans also avoid currently occupied cells. Occupied destinations are left available for queueing, while the collision guard prevents entry until they actually become free.

The result reports explored nodes, total weighted cost, path cells and measured wall-clock computation time. The engine aggregates route searches, including cost-scheduler confirmation queries. A* is spatial, not a search over `(cell,time)` states; future multi-agent conflicts are handled by reservation arbitration and replanning.

For `V` reachable cells and `E ≤ 4V` edges, worst-case search work is approximately `O((V+E) log V)` and storage is `O(V+E)`. Standard-grid BFS distance fields cost `O(V+E)` and supply exact unweighted route distances for feasibility and assignment.

## Reservations and simultaneous movement

For each arrival tick, `CollisionManager` stores:

- `cell → robot`: exclusive destination reservations.
- `(from,to) → robot`: directed movement edges.

A reservation fails if another robot owns its destination or the reverse edge at the same arrival tick. Waiting is a reservation of the current cell. The route reservation helper stops at the first conflict and supports a bounded horizon.

Every real tick uses the following movement arbitration:

1. Index current occupancy and rotate robot priority by tick number.
2. Accept at most one valid, adjacent proposal for each destination.
3. Reject two-way swaps.
4. Record dependencies for robots following another robot's departure.
5. Propagate every rejected/stationary leader through its followers.
6. Reserve and atomically commit the surviving move set.

Following chains and rotations of four or more grid cells are legal. A stopped leader cannot be overwritten by a follower. Destination exclusivity prevents intersection collisions. This takes expected `O(R)` work and memory for `R` robots, excluding hash-table behavior.

A four-tick forecast repeatedly applies this same guard to copies of the current routes. It reserves moving and stationary robots, providing conflict-free planned future occupancy for inspection. Forecasts are **revisable**, not irrevocable promises: service transitions, newly assigned tasks and failures may invalidate them. At the next real tick they are rebuilt, and actual move safety is established again before commit. This is a conservative rolling reservation baseline, not Cooperative A*, WHCA*, or Conflict-Based Search.

Blocked robots wait. After three blocked ticks, staggered occupancy-aware replanning reduces synchronized oscillation. Persistent conflicts can trigger a one-cell sidestep and short hold. Idle blockers park away from shelf pickup cells. Rotating arbitration reduces fixed-ID bias, but neither local detours nor finite lookahead prove deadlock freedom.

## Assignment policies

The engine collects pending orders, sorts by descending priority, and selects a bounded batch. The scheduler further sorts ties by creation time and ID. Only idle robots without tasks are eligible, and all policies use the same reachability/battery feasibility check.

| Policy | Selection |
| --- | --- |
| Random | Shuffle eligible robots with a dedicated seeded stream; greedily take a feasible unused robot |
| Nearest | Greedily minimize Manhattan distance to the pickup |
| Cost | Greedily minimize exact static grid route distance, then run A* on the selected pair before committing |
| Hungarian | Jointly minimize total exact grid approach distance across a rectangular feasible batch |

Cost assignment uses BFS fields to rank pairs efficiently. On this unit-cost static grid their distances equal unweighted A* shortest-path costs. The selected assignment is explicitly confirmed with A*. This avoids an A* run for every possible robot/order pair. Assignment cost does not predict congestion, wait time or future station queues; congestion-aware A* chooses actual movement routes afterwards.

### Hungarian optimization

Orders are rows and available robots are columns. The implementation maintains row/column potentials and reduced-cost slacks, augmenting one matching at a time. Extra dummy columns permit unassigned rows. Their penalty is derived from the input cost range so maximum feasible cardinality wins before minimum total cost. Positive infinity marks forbidden pairs. Negative finite costs and rectangular matrices are supported by the general helper and checked against an exhaustive oracle in tests.

For `B` order rows and `R` robot columns, work is `O(B²(R+B))`; the matrix uses `O(BR)` space. A batch cap avoids solving a fleet-sized cubic problem on every tick. Engine default `B=48`; the scheduler hard cap is 64. This optimizes the current batch's approach distance, not end-to-end warehouse throughput. When robots are scarce, Hungarian cost optimization may leave an expensive high-priority order unmatched within the chosen batch; priority determines batch admission rather than a strict global service guarantee.

## Congestion

Two fields serve different purposes:

1. **Cumulative visits:** increment on every successful move; never decay; displayed as the heatmap.
2. **Recent density:** multiply by `0.96` each tick and add `0.12` on entry; feed into A* costs.

Charging routes use zero congestion weight to avoid needless energy detours. A rejected movement increments `collisionsPrevented`; a blocked streak reaching three ticks increments `congestionEvents`. These are intervention/event counters, not measurements of actual collisions. Actual same-cell and swap collisions are forbidden and tested as invariants.

## Battery safety and failures

Before assignment, estimate:

```text
required_energy = move_energy × (robot→pickup + pickup→packing + packing→charger)
                + action_energy × (pickup_ticks + dropoff_ticks)
                + reserve
```

An infeasible pairing is excluded for every scheduler. An idle robot with an insufficient partial charge requests a charger even above the normal threshold. Chargers have one admission owner. Active robots reassess their minimum return energy and can release a task to recharge before exhaustion; charge-bound moves cannot spend energy needed for the remaining route. Congestion detours and blocked chargers can still prevent completion under adverse layouts; no energy is invented to rescue a stranded robot.

Failure probability is per robot per simulation tick. Temporary repairs are measured in ticks; a separate probability makes a failure permanent. Failures release unfinished task and station ownership but preserve physical occupancy. Work is offered again to operational robots. The scheduler does not allocate work to failed robots.

## Metrics interpretation

Throughput is `completed / simulated_seconds × 3600`, an average since tick zero. Fulfillment is creation-to-completion time, including queueing. Median averages the central two values for an even sample; p95 uses nearest rank. All fulfillment statistics include **completed orders only**. Utilization is task-owning robot ticks divided by all robot ticks, including waiting on an assigned task. Distance/order includes all fleet movement (parking and charging included) divided by completed orders. Idle, charging and battery-wait time are aggregate robot-seconds; battery-wait may overlap movement/idle classifications.
