#ifndef REVERSI_AI_INTERNAL_H
#define REVERSI_AI_INTERNAL_H

/*
 * Shared scoring scale. A terminal win must exceed every heuristic score,
 * and the search's infinity must exceed a terminal win; keep that ordering
 * when changing any of these values.
 */
#define REVERSI_AI_TERMINAL_WIN 100000
#define REVERSI_AI_WIN_THRESHOLD 90000
#define REVERSI_AI_INF 1000000

#endif
