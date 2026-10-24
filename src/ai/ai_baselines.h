#ifndef REVERSI_AI_AI_BASELINES_H
#define REVERSI_AI_AI_BASELINES_H

#include "core/game.h"

#include <stdint.h>

/*
 * Deterministic baseline opponents shared by the legacy benchmark and the
 * experiment platform. They use the same rules and evaluation as the game.
 */

/* xorshift-based generator used for reproducible random play. */
uint64_t ai_baseline_next_random(uint64_t *state);

/* Uniform legal move; returns {-1, -1} when there is no legal move. */
Move ai_baseline_random_move(const GameState *state, uint64_t *rng);

/*
 * One-ply greedy move by the full evaluation. Returns {-1, -1} when there is
 * no legal move.
 */
Move ai_baseline_greedy_move(const GameState *state);

#endif
