# Experiment: tt-vs-nott

- Commit: `babbb23`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000334
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 2, opening ply 4, max plies 100

## Engines

| | A: TT | B: NoTT |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 2 |
| depth | 6 | 6 |
| time budget | 2000 ms | 2000 ms |
| node cap | 0 | 0 |
| TT | on | off |
| endgame solver | off | off |

## Results

| Metric | Value |
|---|---|
| A wins | 4 |
| B wins | 4 |
| Draws | 0 |
| A wins as Black | 4 |
| A wins as White | 0 |
| Illegal games | 0 |
| Timeouts | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.500 (95% CI 0.215..0.785) |
| Elo difference | +0 (95% CI -225..+225) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 224 | 224 |
| Nodes | 1383640 | 3109902 |
| NPS | 211501 | 204936 |
| P50 | 19 ms | 52 ms |
| P95 | 80 ms | 190 ms |
| Max depth | 6 | 6 |
| Cut rate | 23.1% | 20.0% |
| TT hit rate | 1.3% | 0.0% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
