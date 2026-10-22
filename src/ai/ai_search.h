#ifndef REVERSI_AI_SEARCH_H
#define REVERSI_AI_SEARCH_H

#include "ai/ai.h"

/* Internal entry points used by ai_choose_move(). */
bool ai_easy_choose(const GameState *state, const AiConfig *config,
                    Move *out_move, AiStats *out_stats);
bool ai_search_choose(const GameState *state, const AiConfig *config,
                      Move *out_move, AiStats *out_stats);

/*
 * Test variant of ai_search_choose(). It can disable the transposition table
 * and always disables the exact endgame solver, so the result is a plain
 * alpha-beta search at the configured depth. This keeps comparisons against
 * the TT path (and against the endgame solver) independent.
 */
bool ai_search_choose_ex(const GameState *state, const AiConfig *config,
                         bool use_tt, Move *out_move, AiStats *out_stats);

#endif
