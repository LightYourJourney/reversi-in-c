# Experiment: fixed-time-150ms

- Commit: `423aba2`
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
| A wins | 4 |
| B wins | 4 |
| Draws | 0 |
| A wins as Black | 3 |
| A wins as White | 1 |
| Illegal games | 0 |
| Timeouts | 0 |
| Node-limit games | 0 |
| Aborted | 0 |
| A score (draw=0.5) | 0.500 (95% CI 0.150..0.850) |
| Elo difference | +0 (95% CI -301..+301) |

Elo is a two-engine logistic estimate from the A score; draws count as 0.5. The interval is a Wilson interval transformed to Elo. Small samples remain uncertain even when all games go one way.

## Search performance

| Metric | A | B |
|---|---|---|
| Moves | 225 | 223 |
| Nodes | 16259951 | 16447650 |
| NPS | 612404 | 620058 |
| Average | 118 ms | 118 ms |
| P50 | 150 ms | 150 ms |
| P95 | 152 ms | 151 ms |
| Average depth | 7 | 7 |
| Max depth | 13 | 13 |
| Cut rate | 25.2% | 25.3% |
| TT hit rate | 3.5% | 3.6% |
| Timeouts | 175 | 174 |
| Node cap hits | 0 | 0 |
## Anomalies

None.

Raw data: `games.csv`, `moves.csv`, `summary.json`. The HTML report renders the same JSON.
