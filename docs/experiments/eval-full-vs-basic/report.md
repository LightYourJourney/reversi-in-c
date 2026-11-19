# Experiment: eval-full-vs-basic

- Commit: `bfa2582-dirty`
- OS: Darwin 25.6.0 arm64
- CPU: Apple M3
- Compiler: AppleClang 21.0.0.21000401
- Build: Release
- Seed: 20261025
- Games: 8 (completed 8)
- Openings: 4, opening ply 4, max plies 100

## Engines

| | A: FullEval | B: BasicEval |
|---|---|---|
| kind | search | search |
| difficulty | 2 | 1 |
| depth | 5 | 5 |
| time budget | 10000 ms | 10000 ms |
| node cap | 0 | 0 |
| TT | on | on |
| endgame solver | off | off |

## Results

| Metric | Value |
|---|---|
| A wins | 6 |
| B wins | 2 |
| Draws | 0 |
| A wins as Black | 3 |
| A wins as White | 3 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| Excluded (unpaired) | 0 |
| A score (draw=0.5) | 0.750 (95% CI 0.301..0.954) |
| Elo difference | +191 (95% CI -147..+528) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 226 | 222 |
| Nodes | 523957 | 496934 |
| NPS | 240568 | 245400 |
| Average | 9 ms | 9 ms |
| P50 | 8 ms | 7 ms |
| P95 | 26 ms | 24 ms |
| Average depth | 4 | 4 |
| Max depth | 5 | 5 |
| Cut rate | 16.5% | 18.4% |
| TT hit rate | 0.7% | 0.8% |
| Timeouts | 0 | 0 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
