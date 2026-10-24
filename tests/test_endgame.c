#include "ai/ai.h"
#include "ai/ai_endgame.h"
#include "ai/ai_search.h"
#include "core/bitboard.h"
#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct CancelAfter {
    int remaining;
} CancelAfter;

static bool cancel_soon(void *context)
{
    CancelAfter *cancel = context;
    if (cancel->remaining <= 0) {
        return true;
    }
    --cancel->remaining;
    return false;
}

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int empty_count(const GameState *state)
{
    Bitboard bb;
    bitboard_from_board(&state->board, &bb);
    return bitboard_popcount(bitboard_empty(&bb));
}

static bool position_with_empties(uint64_t seed, int target,
                                  GameState *out)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed;
    while (empty_count(&state) > target) {
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &state.board, state.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            return false;
        }
        const Move move = moves[next_random(&rng) % (uint64_t)count];
        if (!game_apply_move(&state, move)) {
            return false;
        }
    }
    *out = state;
    return empty_count(&state) == target;
}

static bool move_is_legal(const GameState *state, Move move)
{
    return rules_is_legal_move(&state->board, state->current_player, move);
}

static void test_exact_against_full_depth(TestStats *stats)
{
    static const int targets[] = {12, 10, 8, 6, 4, 2};
    for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
        for (uint64_t seed = 1; seed <= 2; ++seed) {
            GameState state;
            if (!position_with_empties(0x1234ULL * seed + (uint64_t)targets[t],
                                       targets[t], &state)) {
                continue;
            }
            const int empties = empty_count(&state);

            /* ai_search_choose_ex() disables both the TT and the endgame
               solver, so this is an independent full-depth alpha-beta. */
            AiConfig reference = ai_default_config(AI_DIFFICULTY_HARD, seed);
            reference.max_depth = empties;
            reference.time_budget_ms = 10000;
            Move ref_move = {0, 0};
            AiStats ref_stats = {0};
            const bool ref_ok = ai_search_choose_ex(
                &state, &reference, false, &ref_move, &ref_stats);
            REVERSI_TEST_ASSERT(stats, ref_ok);
            if (ref_stats.timed_out || ref_stats.cancelled) {
                /* The independent full-width search hit its hard 10 s cap
                   (sanitizer builds are several times slower); a partial
                   reference score is not comparable. Debug/Release still
                   cover every position. */
                continue;
            }

            AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, seed);
            config.time_budget_ms = 10000;
            Move move = {-1, -1};
            int score = 0;
            AiStats solve_stats;
            AiEndgameStatus status = AI_ENDGAME_INCOMPLETE;
            const bool solved = ai_endgame_solve(
                &state, &config, empties, &move, &score, &solve_stats,
                &status);
            REVERSI_TEST_ASSERT(stats, solved);
            REVERSI_TEST_ASSERT(stats, status == AI_ENDGAME_EXACT);
            REVERSI_TEST_ASSERT(stats, score == ref_stats.score);
            REVERSI_TEST_ASSERT(stats, move_is_legal(&state, move));
        }
    }
}

static void test_incomplete_classification(TestStats *stats)
{
    GameState state;
    if (!position_with_empties(0xABCDEFULL, 12, &state)) {
        return;
    }
    const int root_moves = rules_count_legal_moves(
        &state.board, state.current_player);
    REVERSI_TEST_ASSERT(stats, root_moves >= 2);
    if (root_moves < 2) {
        return;
    }
    CancelAfter cancel = {1};
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 7);
    config.time_budget_ms = 10000;
    config.should_cancel = cancel_soon;
    config.cancel_context = &cancel;

    Move move = {-1, -1};
    int score = 0;
    AiStats solve_stats;
    AiEndgameStatus status = AI_ENDGAME_EXACT;
    const bool solved = ai_endgame_solve(&state, &config, 12, &move, &score,
                                         &solve_stats, &status);
    REVERSI_TEST_ASSERT(stats, solved);
    REVERSI_TEST_ASSERT(stats, status == AI_ENDGAME_INCOMPLETE);
    REVERSI_TEST_ASSERT(stats, move_is_legal(&state, move));
}

static void test_limits(TestStats *stats)
{
    GameState state;
    if (!position_with_empties(0x55AAULL, 8, &state)) {
        return;
    }
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 1);
    config.time_budget_ms = 1000;
    Move move = {0, 0};
    int score = 0;
    AiEndgameStatus status = AI_ENDGAME_EXACT;
    REVERSI_TEST_ASSERT(stats,
        !ai_endgame_solve(&state, &config, 4, &move, &score, NULL, &status));

    GameState opening;
    game_init(&opening);
    REVERSI_TEST_ASSERT(stats,
        !ai_endgame_solve(&opening, &config, 12, &move, &score, NULL,
                          &status));

    REVERSI_TEST_ASSERT(stats,
        !ai_endgame_solve(NULL, &config, 12, &move, &score, NULL, &status));
}

int main(void)
{
    TestStats stats = {0, 0};
    REVERSI_RUN_TEST(&stats, test_exact_against_full_depth);
    REVERSI_RUN_TEST(&stats, test_incomplete_classification);
    REVERSI_RUN_TEST(&stats, test_limits);
    return reversi_test_summary(&stats, "ai.endgame");
}
