# Experiment: depth-4-vs-6

- Commit: `bfa2582-dirty`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000401
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 4, opening ply 4, max plies 100

## Engines

| | A: Depth4 | B: Depth6 |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 2 |
| depth | 4 | 6 |
| time budget | 10000 ms | 10000 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | off | off |

## Results

| Metric | Value |
|---|---|
| A wins | 1 |
| B wins | 7 |
| Draws | 0 |
| A wins as Black | 0 |
| A wins as White | 1 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| Excluded (unpaired) | 0 |
| A score (draw=0.5) | 0.125 (95% CI 0.013..0.604) |
| Elo difference | -338 (95% CI -750..+74) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 220 | 228 |
| Nodes | 135653 | 1624151 |
| NPS | 48955 | 52284 |
| Average | 12 ms | 136 ms |
| P50 | 4 ms | 66 ms |
| P95 | 55 ms | 501 ms |
| Average depth | 3 | 5 |
| Max depth | 4 | 6 |
| Cut rate | 18.5% | 25.5% |
| TT hit rate | 0.4% | 1.6% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
