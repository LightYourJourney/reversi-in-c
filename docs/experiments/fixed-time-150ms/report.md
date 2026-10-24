# Experiment: fixed-time-150ms

- Commit: `ce8f7f4`
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
| A wins | 5 |
| B wins | 3 |
| Draws | 0 |
| A wins as Black | 1 |
| A wins as White | 4 |
| Illegal games | 0 |
| Timeouts | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.625 (95% CI 0.306..0.863) |
| Elo difference | +89 (95% CI -142..+320) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 222 | 226 |
| Nodes | 9260108 | 9106963 |
| NPS | 337566 | 334593 |
| P50 | 151 ms | 150 ms |
| P95 | 163 ms | 168 ms |
| Max depth | 13 | 13 |
| Timeouts | 174 | 173 |
| Node cap hits | 0 | 0 |

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
