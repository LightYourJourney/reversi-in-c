# Experiment: fixed-time-150ms

- Commit: `d7c6bb8`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 2, opening ply 4, max plies 100

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
| A wins | 2 |
| B wins | 6 |
| Draws | 0 |
| A wins as Black | 0 |
| A wins as White | 2 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.250 (95% CI 0.046..0.699) |
| Elo difference | -191 (95% CI -528..+147) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 225 | 223 |
| Nodes | 5684672 | 5743416 |
| NPS | 203883 | 204087 |
| Average | 123 ms | 126 ms |
| P50 | 151 ms | 151 ms |
| P95 | 188 ms | 184 ms |
| Average depth | 6 | 6 |
| Max depth | 13 | 14 |
| Cut rate | 25.0% | 25.3% |
| TT hit rate | 3.0% | 3.0% |
| Timeouts | 174 | 174 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
