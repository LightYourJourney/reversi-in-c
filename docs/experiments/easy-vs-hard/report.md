# Experiment: easy-vs-hard

- Commit: `bfa2582-dirty`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000401
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 4, opening ply 4, max plies 100

## Engines

| | A: Easy | B: Hard |
|---|---|---|
| kind | ai | ai |
| difficulty | 0 | 2 |
| depth | 8 | 6 |
| time budget | 500 ms | 10000 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | on | on |

## Results

| Metric | Value |
|---|---|
| A wins | 0 |
| B wins | 8 |
| Draws | 0 |
| A wins as Black | 0 |
| A wins as White | 0 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| Excluded (unpaired) | 0 |
| A score (draw=0.5) | 0.000 (95% CI 0.000..0.490) |
| Elo difference | -1200 (95% CI -1200..-7) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 217 | 230 |
| Nodes | 1391 | 1908489 |
| NPS | 86938 | 70380 |
| Average | 0 ms | 117 ms |
| P50 | 0 ms | 69 ms |
| P95 | 1 ms | 391 ms |
| Average depth | 1 | 6 |
| Max depth | 1 | 12 |
| Cut rate | 0.0% | 27.9% |
| TT hit rate | 0.0% | 2.3% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
