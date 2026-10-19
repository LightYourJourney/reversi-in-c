#ifndef REVERSI_AI_SEARCH_H
#define REVERSI_AI_SEARCH_H

#include "ai/ai.h"

/* Internal entry points used by ai_choose_move(). */
bool ai_easy_choose(const GameState *state, const AiConfig *config,
                    Move *out_move, AiStats *out_stats);
bool ai_search_choose(const GameState *state, const AiConfig *config,
                      Move *out_move, AiStats *out_stats);

#endif
