#include "ai/ai_baselines.h"

#include "ai/ai.h"
#include "core/rules.h"

#include <limits.h>

uint64_t ai_baseline_next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int legal_moves(const GameState *state, Move *moves)
{
    return rules_generate_legal_moves(&state->board,
                                      state->current_player, moves,
                                      REVERSI_BOARD_CELL_COUNT);
}

Move ai_baseline_random_move(const GameState *state, uint64_t *rng)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = legal_moves(state, moves);
    if (count <= 0) {
        return (Move){-1, -1};
    }
    return moves[ai_baseline_next_random(rng) % (uint64_t)count];
}

Move ai_baseline_greedy_move(const GameState *state)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = legal_moves(state, moves);
    if (count <= 0) {
        return (Move){-1, -1};
    }

    Move best = moves[0];
    int best_score = INT_MIN;
    for (int i = 0; i < count; ++i) {
        GameState child = *state;
        if (rules_apply_move(&child.board, state->current_player,
                             moves[i]) < 0) {
            continue;
        }
        child.current_player = stone_opponent(state->current_player);
        child.result = GAME_RESULT_ONGOING;
        AiEvaluation evaluation;
        ai_evaluate(&child, &evaluation);
        const int score = -evaluation.total;
        if (score > best_score) {
            best_score = score;
            best = moves[i];
        }
    }
    return best;
}
