# Experiment: depth-4-vs-6

- Commit: `423aba2`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 2, opening ply 4, max plies 100

## Engines

| | A: Depth4 | B: Depth6 |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 2 |
| depth | 4 | 6 |
| time budget | 5000 ms | 5000 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | off | off |

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
| Moves | 222 | 226 |
| Nodes | 130038 | 1485830 |
| NPS | 411513 | 489726 |
| Average | 1 ms | 13 ms |
| P50 | 1 ms | 11 ms |
| P95 | 4 ms | 35 ms |
| Average depth | 3 | 5 |
| Max depth | 4 | 6 |
| Cut rate | 18.3% | 26.1% |
| TT hit rate | 0.4% | 1.8% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
