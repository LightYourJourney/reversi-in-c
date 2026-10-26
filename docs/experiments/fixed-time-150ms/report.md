# Experiment: fixed-time-150ms

- Commit: `6ae6e01`
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
| A wins | 3 |
| B wins | 5 |
| Draws | 0 |
| A wins as Black | 2 |
| A wins as White | 1 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| Excluded (unpaired) | 0 |
| A score (draw=0.5) | 0.375 (95% CI 0.092..0.781) |
| Elo difference | -89 (95% CI -398..+220) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 224 | 224 |
| Nodes | 6713403 | 6855389 |
| NPS | 239962 | 249242 |
| Average | 124 ms | 122 ms |
| P50 | 151 ms | 151 ms |
| P95 | 178 ms | 181 ms |
| Average depth | 6 | 6 |
| Max depth | 14 | 13 |
| Cut rate | 24.1% | 24.2% |
| TT hit rate | 2.7% | 2.9% |
| Timeouts | 176 | 174 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
