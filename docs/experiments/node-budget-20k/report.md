# Experiment: node-budget-20k

- Commit: `ce8f7f4`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 4 (completed 4)
- Openings: 2, opening ply 2, max plies 100

## Engines

| | A: Node20kA | B: Node20kB |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 2 |
| depth | 32 | 32 |
| time budget | 10000 ms | 10000 ms |
| node cap | 20000 | 20000 |
| TT | on | on |
| endgame solver | off | off |

## Results

| Metric | Value |
|---|---|
| A wins | 2 |
| B wins | 2 |
| Draws | 0 |
| A wins as Black | 0 |
| A wins as White | 2 |
| Illegal games | 0 |
| Timeouts | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.500 (95% CI 0.150..0.850) |
| Elo difference | +0 (95% CI -301..+301) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 115 | 115 |
| Nodes | 2012302 | 2012302 |
| NPS | 425794 | 423197 |
| P50 | 43 ms | 43 ms |
| P95 | 75 ms | 74 ms |
| Max depth | 10 | 10 |
| Timeouts | 0 | 0 |
| Node cap hits | 97 | 97 |

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
