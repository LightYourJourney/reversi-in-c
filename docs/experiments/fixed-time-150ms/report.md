# Experiment: fixed-time-150ms

- Commit: `bfa2582-dirty`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000401
- Build: Release
- Seed: 20261025
- Games: 8 (completed 7)
- Openings: 4, opening ply 4, max plies 100

## Engines

| | A: FixedTimeA | B: FixedTimeB |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 2 |
| depth | 32 | 32 |
| time budget | 150 ms | 150 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | on | on |

## Results

| Metric | Value |
|---|---|
| A wins | 3 |
| B wins | 3 |
| Draws | 0 |
| A wins as Black | 1 |
| A wins as White | 2 |
| Illegal games | 0 |
| Timeouts | 1 |
| Node-limit games | 0 |
| Aborted | 0 |
| Excluded (unpaired) | 1 |
| A score (draw=0.5) | 0.500 (95% CI 0.125..0.875) |
| Elo difference | +0 (95% CI -338..+338) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 215 | 220 |
| Nodes | 2063550 | 2139967 |
| NPS | 64969 | 69299 |
| Average | 147 ms | 140 ms |
| P50 | 156 ms | 155 ms |
| P95 | 245 ms | 228 ms |
| Average depth | 5 | 5 |
| Max depth | 12 | 12 |
| Cut rate | 23.3% | 24.0% |
| TT hit rate | 2.2% | 2.4% |
| Timeouts | 178 | 178 |
| Node cap hits | 0 | 0 |
## Anomalies

- Game 2: illegal=no timeout=yes node_limit=no aborted=no

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
