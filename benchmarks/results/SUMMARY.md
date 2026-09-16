# Measured benchmark results

Generated 2026-09-15T23:49:16.443Z. Intel(R) Core(TM) i7-9700 CPU @ 3.00GHz; win32 x64; Release. 300 ticks at 1 simulated second per tick; seed 42; layout large; arrivals 10/s; initial backlog 100; cap 10000. Wall time excludes construction and JSON output.

| Robots | Scheduler | Completed | Orders/h | Avg fulfillment (s) | p95 (s) | Utilization | Wall time (s) |
| ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | random | 18 | 216.0 | 166.4 | 287.0 | 98.9% | 0.050 |
| 10 | nearest | 20 | 240.0 | 145.5 | 275.0 | 98.8% | 0.061 |
| 10 | cost | 20 | 240.0 | 145.5 | 275.0 | 98.8% | 0.049 |
| 10 | hungarian | 39 | 468.0 | 105.2 | 238.0 | 97.4% | 0.065 |
| 25 | random | 45 | 540.0 | 162.3 | 255.0 | 98.3% | 0.115 |
| 25 | nearest | 53 | 636.0 | 139.7 | 264.0 | 98.5% | 0.119 |
| 25 | cost | 53 | 636.0 | 139.7 | 264.0 | 98.5% | 0.110 |
| 25 | hungarian | 79 | 948.0 | 105.6 | 276.0 | 97.7% | 0.137 |
| 50 | random | 76 | 912.0 | 157.2 | 257.0 | 98.8% | 0.156 |
| 50 | nearest | 93 | 1116.0 | 130.5 | 253.0 | 98.6% | 0.161 |
| 50 | cost | 91 | 1092.0 | 126.7 | 235.0 | 98.8% | 0.161 |
| 50 | hungarian | 130 | 1560.0 | 105.3 | 213.0 | 98.1% | 0.191 |
| 100 | random | 136 | 1632.0 | 154.5 | 257.0 | 98.5% | 0.442 |
| 100 | nearest | 177 | 2124.0 | 116.3 | 258.0 | 98.1% | 0.516 |
| 100 | cost | 177 | 2124.0 | 118.2 | 241.0 | 98.3% | 0.648 |
| 100 | hungarian | 209 | 2508.0 | 112.6 | 236.0 | 97.7% | 0.540 |
| 250 | random | 223 | 2676.0 | 167.7 | 285.0 | 97.3% | 1.612 |
| 250 | nearest | 292 | 3504.0 | 123.4 | 259.0 | 96.5% | 1.608 |
| 250 | cost | 275 | 3300.0 | 120.8 | 254.0 | 95.7% | 1.739 |
| 250 | hungarian | 284 | 3408.0 | 114.7 | 234.0 | 95.1% | 2.058 |
| 500 | random | 249 | 2988.0 | 160.5 | 268.0 | 93.9% | 3.579 |
| 500 | nearest | 212 | 2544.0 | 94.7 | 246.0 | 91.6% | 5.819 |
| 500 | cost | 238 | 2856.0 | 104.2 | 250.0 | 92.4% | 5.062 |
| 500 | hungarian | 168 | 2016.0 | 74.5 | 220.0 | 91.5% | 5.520 |
| 1000 | random | 209 | 2508.0 | 153.2 | 263.0 | 86.2% | 9.697 |
| 1000 | nearest | 131 | 1572.0 | 57.1 | 190.0 | 81.5% | 13.041 |
| 1000 | cost | 145 | 1740.0 | 59.7 | 213.0 | 80.8% | 12.938 |
| 1000 | hungarian | 142 | 1704.0 | 57.5 | 207.0 | 80.3% | 11.411 |

Fulfillment percentiles include completed orders only. These short fixed-horizon runs are a smoke/scaling experiment, not steady-state capacity estimates. Work remaining is preserved in the raw results.
