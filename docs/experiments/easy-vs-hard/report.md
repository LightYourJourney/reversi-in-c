# Experiment: easy-vs-hard

- Commit: `d7c6bb8`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 2, opening ply 4, max plies 100

## Engines

| | A: Easy | B: Hard |
|---|---|---|
| kind | ai | ai |
| difficulty | 0 | 2 |
| depth | 8 | 6 |
| time budget | 500 ms | 200 ms |
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
| Moves | 218 | 230 |
| Nodes | 1292 | 2148642 |
| NPS | 184571 | 341379 |
| Average | 0 ms | 27 ms |
| P50 | 0 ms | 20 ms |
| P95 | 0 ms | 79 ms |
| Average depth | 1 | 6 |
| Max depth | 1 | 12 |
| Cut rate | 0.0% | 29.3% |
| TT hit rate | 0.0% | 2.7% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
