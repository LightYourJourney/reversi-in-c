#ifndef REVERSI_AI_AI_ENDGAME_H
#define REVERSI_AI_AI_ENDGAME_H

#include "ai/ai.h"

typedef enum AiEndgameStatus {
    AI_ENDGAME_EXACT = 0,
    AI_ENDGAME_INCOMPLETE
} AiEndgameStatus;

/*
 * Full-width solve of the remaining game. Returns false when the position has
 * more than max_empties empty squares, no legal move, or invalid arguments.
 *
 * On success, out_status distinguishes a complete solve (exact score and best
 * move) from one stopped by the time budget or the cancel callback. In the
 * incomplete case out_move is the best move found so far, but out_score is
 * not an exact game value.
 */
bool ai_endgame_solve(const GameState *state, const AiConfig *config,
                      int max_empties, Move *out_move, int *out_score,
                      AiStats *out_stats, AiEndgameStatus *out_status);

#endif
