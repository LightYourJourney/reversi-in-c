# Experiment: random-100

- Commit: `d7c6bb8`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 100 (completed 100)
- Openings: 4, opening ply 0, max plies 100

## Engines

| | A: RandomA | B: RandomB |
|---|---|---|
| kind | random | random |
| difficulty | 2 | 2 |
| depth | 8 | 8 |
| time budget | 500 ms | 500 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | on | on |

## Results

| Metric | Value |
|---|---|
| A wins | 47 |
| B wins | 45 |
| Draws | 8 |
| A wins as Black | 24 |
| A wins as White | 23 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.510 (95% CI 0.376..0.643) |
| Elo difference | +7 (95% CI -88..+102) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 3003 | 2995 |
| Nodes | 0 | 0 |
| NPS | 0 | 0 |
| Average | 0 ms | 0 ms |
| P50 | 0 ms | 0 ms |
| P95 | 0 ms | 0 ms |
| Average depth | 0 | 0 |
| Max depth | 0 | 0 |
| Cut rate | 0.0% | 0.0% |
| TT hit rate | 0.0% | 0.0% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
