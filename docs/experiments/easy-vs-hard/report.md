# Experiment: easy-vs-hard

- Commit: `babbb23`
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
| A wins | 1 |
| B wins | 7 |
| Draws | 0 |
| A wins as Black | 0 |
| A wins as White | 1 |
| Illegal games | 0 |
| Timeouts | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.125 (95% CI 0.022..0.471) |
| Elo difference | -338 (95% CI -656..-20) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 216 | 231 |
| Nodes | 1302 | 2365816 |
| NPS | 130200 | 262227 |
| P50 | 0 ms | 25 ms |
| P95 | 0 ms | 132 ms |
| Max depth | 1 | 12 |
| Cut rate | 0.0% | 30.0% |
| TT hit rate | 0.0% | 4.2% |
| Timeouts | 0 | 5 |
| Node cap hits | 0 | 0 |

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
