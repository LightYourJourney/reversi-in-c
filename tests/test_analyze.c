#include "ai/ai_analyze.h"
#include "ai/ai_endgame.h"
#include "ai/ai_search.h"
#include "core/bitboard.h"
#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static bool position_with_empties(uint64_t seed, int target, GameState *out)
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

static AiConfig analysis_config(AiDifficulty difficulty, int depth,
                                int budget)
{
    AiConfig config = ai_default_config(difficulty, 1234);
    config.max_depth = depth;
    config.time_budget_ms = budget;
    return config;
}

static void test_multipv_ordering(TestStats *stats)
{
    GameState state;
    game_init(&state);
    const GameState before = state;
    AiConfig config = analysis_config(AI_DIFFICULTY_HARD, 5, 10000);

    AiAnalysis analysis;
    REVERSI_TEST_ASSERT(stats, ai_analyze_position(&state, &config, 3,
                                                   &analysis));
    REVERSI_TEST_ASSERT(stats, analysis.line_count >= 2);
    REVERSI_TEST_ASSERT(stats, analysis.depth >= 1);
    REVERSI_TEST_ASSERT(stats, game_state_equal(&state, &before));

    for (int i = 0; i < analysis.line_count; ++i) {
        const AiAnalysisLine *line = &analysis.lines[i];
        REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
            &state.board, state.current_player, line->move));
        REVERSI_TEST_ASSERT(stats, line->depth == analysis.depth);
        REVERSI_TEST_ASSERT(stats, line->pv_length >= 1);
        REVERSI_TEST_ASSERT(stats, line->pv[0].row == line->move.row
                            && line->pv[0].col == line->move.col);
        if (i > 0) {
            REVERSI_TEST_ASSERT(stats,
                                analysis.lines[i - 1].score >= line->score);
            REVERSI_TEST_ASSERT(stats,
                                !(analysis.lines[i - 1].move.row == line->move.row
                                  && analysis.lines[i - 1].move.col == line->move.col));
        }
    }

    /* The best score must agree with the regular move chooser at the same
       depth; equal-scoring moves may be ordered differently. */
    Move chosen;
    AiStats chosen_stats;
    REVERSI_TEST_ASSERT(stats, ai_search_choose(&state, &config, &chosen,
                                                &chosen_stats));
    REVERSI_TEST_ASSERT(stats, chosen_stats.score == analysis.lines[0].score);
}

static void test_pv_legality(TestStats *stats)
{
    GameState state;
    game_init(&state);
    AiConfig config = analysis_config(AI_DIFFICULTY_HARD, 5, 10000);
    AiAnalysis analysis;
    REVERSI_TEST_ASSERT(stats, ai_analyze_position(&state, &config, 3,
                                                   &analysis));

    for (int i = 0; i < analysis.line_count; ++i) {
        const AiAnalysisLine *line = &analysis.lines[i];
        GameState replay = state;
        for (int j = 0; j < line->pv_length; ++j) {
            REVERSI_TEST_ASSERT(stats, !game_is_over(&replay));
            REVERSI_TEST_ASSERT(stats, !game_must_pass(&replay));
            REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
                &replay.board, replay.current_player, line->pv[j]));
            REVERSI_TEST_ASSERT(stats, game_apply_move(&replay, line->pv[j]));
        }
    }
}

static void test_exact_endgame(TestStats *stats)
{
    GameState state;
    if (!position_with_empties(0x5151ULL, 4, &state)) {
        return;
    }
    AiConfig config = analysis_config(AI_DIFFICULTY_HARD, 8, 10000);
    AiAnalysis analysis;
    REVERSI_TEST_ASSERT(stats, ai_analyze_position(&state, &config, 3,
                                                   &analysis));
    REVERSI_TEST_ASSERT(stats, analysis.exact);
    REVERSI_TEST_ASSERT(stats, analysis.depth >= 4);

    AiEndgameStatus status = AI_ENDGAME_INCOMPLETE;
    Move move;
    int score = 0;
    AiStats solve_stats;
    REVERSI_TEST_ASSERT(stats, ai_endgame_solve(
        &state, &config, 12, &move, &score, &solve_stats, &status));
    REVERSI_TEST_ASSERT(stats, status == AI_ENDGAME_EXACT);
    REVERSI_TEST_ASSERT(stats, score == analysis.lines[0].score);
    REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
        &state.board, state.current_player, move));
    REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
        &state.board, state.current_player, analysis.lines[0].move));
}

typedef struct AlwaysCancel {
    int unused;
} AlwaysCancel;

static bool cancel_now(void *context)
{
    (void)context;
    return true;
}

static void test_cancellation(TestStats *stats)
{
    GameState state;
    game_init(&state);
    AiConfig config = analysis_config(AI_DIFFICULTY_HARD, 8, 10000);
    AlwaysCancel cancel = {0};
    config.should_cancel = cancel_now;
    config.cancel_context = &cancel;

    AiAnalysis analysis;
    REVERSI_TEST_ASSERT(stats, !ai_analyze_position(&state, &config, 3,
                                                    &analysis));
    REVERSI_TEST_ASSERT(stats, analysis.stats.cancelled);
    REVERSI_TEST_ASSERT(stats, analysis.line_count == 0);
}

static void test_black_perspective(TestStats *stats)
{
    GameState state;
    game_init(&state);
    const Move d3 = {5, 3};
    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, d3));
    REVERSI_TEST_ASSERT(stats, state.current_player == STONE_WHITE);

    AiEvaluation side;
    ai_evaluate(&state, &side);
    AiEvaluation black;
    ai_evaluation_to_black(&side, state.current_player, &black);
    REVERSI_TEST_ASSERT(stats, black.total == -side.total);
    REVERSI_TEST_ASSERT(stats, black.corners == -side.corners);
    REVERSI_TEST_ASSERT(stats, black.mobility == -side.mobility);
    REVERSI_TEST_ASSERT(stats, black.phase == side.phase);
    REVERSI_TEST_ASSERT(stats, black.empty_cells == side.empty_cells);

    AiEvaluation already_black;
    ai_evaluation_to_black(&side, STONE_BLACK, &already_black);
    REVERSI_TEST_ASSERT(stats, already_black.total == side.total);
}

static void test_terminal(TestStats *stats)
{
    GameState state;
    memset(&state, 0, sizeof(state));
    board_clear(&state.board);
    board_set(&state.board, 0, 0, STONE_BLACK);
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_BLACK_WIN;

    AiConfig config = analysis_config(AI_DIFFICULTY_HARD, 4, 1000);
    AiAnalysis analysis;
    REVERSI_TEST_ASSERT(stats, !ai_analyze_position(&state, &config, 3,
                                                    &analysis));
    REVERSI_TEST_ASSERT(stats, analysis.line_count == 0);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_multipv_ordering);
    REVERSI_RUN_TEST(&stats, test_pv_legality);
    REVERSI_RUN_TEST(&stats, test_exact_endgame);
    REVERSI_RUN_TEST(&stats, test_cancellation);
    REVERSI_RUN_TEST(&stats, test_black_perspective);
    REVERSI_RUN_TEST(&stats, test_terminal);
    return reversi_test_summary(&stats, "ai.analyze");
}
