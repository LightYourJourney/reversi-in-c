# Experiment: random-100

- Commit: `ce8f7f4`
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
| A wins | 45 |
| B wins | 49 |
| Draws | 6 |
| A wins as Black | 21 |
| A wins as White | 24 |
| Illegal games | 0 |
| Timeouts | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.480 (95% CI 0.385..0.577) |
| Elo difference | -14 (95% CI -82..+54) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 3006 | 2992 |
| Nodes | 0 | 0 |
| NPS | 0 | 0 |
| P50 | 0 ms | 0 ms |
| P95 | 0 ms | 0 ms |
| Max depth | 0 | 0 |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
