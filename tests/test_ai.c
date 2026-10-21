#include "ai/ai.h"
#include "ai/ai_search.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

#include <stdint.h>
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

static void play_random(GameState *state, uint64_t *rng, int moves)
{
    for (int i = 0; i < moves && state->result == GAME_RESULT_ONGOING; ++i) {
        if (game_must_pass(state)) {
            (void)game_pass(state);
            continue;
        }
        Move legal[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &state->board, state->current_player, legal,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            break;
        }
        (void)game_apply_move(state, legal[next_random(rng) % (uint64_t)count]);
    }
}

static void build_terminal_with_empty(GameState *state)
{
    static const char *const rows[REVERSI_BOARD_SIZE] = {
        "WWWWWWW.",
        "WWWWBWWB",
        "WBWWWWWB",
        "WBWWWBWB",
        "WWWWBBWB",
        "WBWWBWWB",
        "WWBBBWBB",
        "WBBBBBBB"
    };

    game_init(state);
    board_clear(&state->board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const char cell = rows[row][col];
            const Stone stone = cell == 'B' ? STONE_BLACK
                : (cell == 'W' ? STONE_WHITE : STONE_EMPTY);
            (void)board_set(&state->board, row, col, stone);
        }
    }
    state->current_player = STONE_BLACK;
    state->result = GAME_RESULT_WHITE_WIN;
}

static void build_one_move_white(GameState *state)
{
    static const char *const rows[REVERSI_BOARD_SIZE] = {
        "WB.WWWW.",
        "WWBBBWWB",
        "WBWWWWWB",
        "WBWWWBWB",
        "WWWWBBWB",
        "WBWWBWWB",
        "WWBBBWBB",
        "WBBBBBBB"
    };

    game_init(state);
    board_clear(&state->board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const char cell = rows[row][col];
            const Stone stone = cell == 'B' ? STONE_BLACK
                : (cell == 'W' ? STONE_WHITE : STONE_EMPTY);
            (void)board_set(&state->board, row, col, stone);
        }
    }
    state->current_player = STONE_WHITE;
    state->result = GAME_RESULT_ONGOING;
}

static void test_config_and_names(TestStats *stats)
{
    const AiConfig easy = ai_default_config(AI_DIFFICULTY_EASY, 7);
    const AiConfig medium = ai_default_config(AI_DIFFICULTY_MEDIUM, 7);
    const AiConfig hard = ai_default_config(AI_DIFFICULTY_HARD, 7);

    REVERSI_TEST_ASSERT(stats, ai_config_is_valid(&easy));
    REVERSI_TEST_ASSERT(stats, ai_config_is_valid(&medium));
    REVERSI_TEST_ASSERT(stats, ai_config_is_valid(&hard));
    REVERSI_TEST_ASSERT(stats, easy.max_depth < medium.max_depth);
    REVERSI_TEST_ASSERT(stats, medium.max_depth < hard.max_depth);
    REVERSI_TEST_ASSERT(stats, easy.randomness > 0);
    REVERSI_TEST_ASSERT(stats, medium.randomness == 0);
    REVERSI_TEST_ASSERT(stats, hard.time_budget_ms > medium.time_budget_ms);
    REVERSI_TEST_ASSERT(stats, easy.seed == 7);
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(NULL));

    AiConfig bad = easy;
    bad.difficulty = (AiDifficulty)99;
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(&bad));
    bad = easy;
    bad.max_depth = 0;
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(&bad));
    bad.max_depth = 33;
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(&bad));
    bad = easy;
    bad.time_budget_ms = 0;
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(&bad));
    bad = easy;
    bad.randomness = -1;
    REVERSI_TEST_ASSERT(stats, !ai_config_is_valid(&bad));

    REVERSI_TEST_ASSERT(stats,
        strcmp(ai_difficulty_name(AI_DIFFICULTY_EASY), "Easy") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ai_difficulty_name(AI_DIFFICULTY_MEDIUM), "Medium") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ai_difficulty_name(AI_DIFFICULTY_HARD), "Hard") == 0);
    REVERSI_TEST_ASSERT(stats, ai_controller_for_difficulty(
        AI_DIFFICULTY_EASY) != NULL);
    REVERSI_TEST_ASSERT(stats, ai_controller_for_difficulty(
        AI_DIFFICULTY_HARD) != NULL);
    REVERSI_TEST_ASSERT(stats, ai_controller_for_difficulty(
        (AiDifficulty)99) == NULL);
}

static void test_evaluation(TestStats *stats)
{
    GameState state;
    game_init(&state);
    AiEvaluation evaluation;
    ai_evaluate(&state, &evaluation);
    REVERSI_TEST_ASSERT(stats, evaluation.phase == AI_PHASE_OPENING);
    REVERSI_TEST_ASSERT(stats, evaluation.empty_cells == 60);
    REVERSI_TEST_ASSERT(stats, evaluation.corners == 0);
    REVERSI_TEST_ASSERT(stats, evaluation.mobility == 0);
    REVERSI_TEST_ASSERT(stats, evaluation.discs == 0);
    REVERSI_TEST_ASSERT(stats, evaluation.total == 0);
    REVERSI_TEST_ASSERT(stats, !ai_is_terminal(&state));

    board_clear(&state.board);
    REVERSI_TEST_ASSERT(stats,
        board_set(&state.board, 0, 0, STONE_BLACK));
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_ONGOING;
    ai_evaluate(&state, &evaluation);
    REVERSI_TEST_ASSERT(stats, evaluation.corners == 1);
    REVERSI_TEST_ASSERT(stats, evaluation.positional == 0);
    REVERSI_TEST_ASSERT(stats, evaluation.total > 0);

    board_clear(&state.board);
    REVERSI_TEST_ASSERT(stats,
        board_set(&state.board, 1, 1, STONE_BLACK));
    ai_evaluate(&state, &evaluation);
    REVERSI_TEST_ASSERT(stats, evaluation.positional == -50);
    REVERSI_TEST_ASSERT(stats, evaluation.corners == 0);
    REVERSI_TEST_ASSERT(stats, evaluation.total < 0);

    REVERSI_TEST_ASSERT(stats, ai_positional_value(0, 0) == 0);
    REVERSI_TEST_ASSERT(stats, ai_positional_value(1, 1) == -50);
    REVERSI_TEST_ASSERT(stats, ai_positional_value(-1, 0) == 0);

    build_terminal_with_empty(&state);
    REVERSI_TEST_ASSERT(stats, ai_is_terminal(&state));
    REVERSI_TEST_ASSERT(stats, ai_terminal_score(&state) < -90000);
    state.current_player = STONE_WHITE;
    REVERSI_TEST_ASSERT(stats, ai_terminal_score(&state) > 90000);

    board_clear(&state.board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = ((row + col) % 2 == 0)
                ? STONE_BLACK : STONE_WHITE;
            REVERSI_TEST_ASSERT(stats,
                board_set(&state.board, row, col, stone));
        }
    }
    state.current_player = STONE_BLACK;
    REVERSI_TEST_ASSERT(stats, ai_terminal_score(&state) == 0);
}

static void test_difficulty_evaluation(TestStats *stats)
{
    GameState state;
    game_init(&state);
    board_clear(&state.board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            (void)board_set(&state.board, row, col, STONE_WHITE);
        }
    }
    for (int row = 0; row < REVERSI_BOARD_SIZE; row += REVERSI_BOARD_SIZE - 1) {
        for (int col = 0; col < REVERSI_BOARD_SIZE;
             col += REVERSI_BOARD_SIZE - 1) {
            (void)board_set(&state.board, row, col, STONE_BLACK);
        }
    }
    for (int col = 1; col <= 3; ++col) {
        (void)board_set(&state.board, 0, col, STONE_BLACK);
    }
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_BLACK_WIN;

    AiEvaluation full;
    AiEvaluation hard;
    AiEvaluation medium;
    AiEvaluation easy;
    ai_evaluate(&state, &full);
    ai_evaluate_for_difficulty(&state, AI_DIFFICULTY_HARD, &hard);
    ai_evaluate_for_difficulty(&state, AI_DIFFICULTY_MEDIUM, &medium);
    ai_evaluate_for_difficulty(&state, AI_DIFFICULTY_EASY, &easy);

    REVERSI_TEST_ASSERT(stats, full.stability > 0);
    REVERSI_TEST_ASSERT(stats, hard.total == full.total);
    REVERSI_TEST_ASSERT(stats, medium.total == easy.total);
    REVERSI_TEST_ASSERT(stats, medium.stability == full.stability);
    REVERSI_TEST_ASSERT(stats, medium.frontier == full.frontier);
    REVERSI_TEST_ASSERT(stats, medium.parity == full.parity);
    REVERSI_TEST_ASSERT(stats, medium.total != full.total);
    REVERSI_TEST_ASSERT(stats, medium.phase == full.phase);

    ai_evaluate_for_difficulty(NULL, AI_DIFFICULTY_HARD, NULL);
    ai_evaluate_for_difficulty(&state, AI_DIFFICULTY_HARD, NULL);
}

static void test_search_returns_legal_moves(TestStats *stats)
{
    GameState state;
    game_init(&state);
    const GameState before = state;

    const AiDifficulty difficulties[3] = {
        AI_DIFFICULTY_EASY, AI_DIFFICULTY_MEDIUM, AI_DIFFICULTY_HARD
    };
    for (int i = 0; i < 3; ++i) {
        AiConfig config = ai_default_config(difficulties[i], 11);
        if (difficulties[i] != AI_DIFFICULTY_EASY) {
            config.time_budget_ms = 10000;
            config.max_depth = 4;
        }
        Move move = {0, 0};
        AiStats search;
        REVERSI_TEST_ASSERT(stats,
            ai_choose_move(&state, &config, &move, &search));
        REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
            &state.board, state.current_player, move));
        REVERSI_TEST_ASSERT(stats, search.nodes > 0);
        REVERSI_TEST_ASSERT(stats, search.depth >= 1);
        REVERSI_TEST_ASSERT(stats, !search.cancelled);
    }
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));
}

static void test_search_many_positions(TestStats *stats)
{
    uint64_t rng = 0x123456789ABCDEF0ULL;
    GameState state;
    game_init(&state);

    for (int game = 0; game < 4; ++game) {
        game_init(&state);
        for (int ply = 0; ply < 24; ++ply) {
            play_random(&state, &rng, 1);
            if (state.result != GAME_RESULT_ONGOING
                || game_must_pass(&state)) {
                continue;
            }
            const GameState before = state;

            AiConfig easy = ai_default_config(AI_DIFFICULTY_EASY, (unsigned)ply);
            AiConfig medium = ai_default_config(AI_DIFFICULTY_MEDIUM,
                                                (unsigned)ply);
            AiConfig hard = ai_default_config(AI_DIFFICULTY_HARD,
                                              (unsigned)ply);
            medium.time_budget_ms = 10000;
            medium.max_depth = 4;
            hard.time_budget_ms = 10000;
            hard.max_depth = 4;

            const AiConfig configs[3] = { easy, medium, hard };
            for (int i = 0; i < 3; ++i) {
                Move move = {0, 0};
                AiStats search;
                REVERSI_TEST_ASSERT(stats,
                    ai_choose_move(&state, &configs[i], &move, &search));
                REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
                    &state.board, state.current_player, move));
                REVERSI_TEST_ASSERT(stats, search.depth >= 1);
            }
            REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));
        }
    }
}

static void test_tt_matches_plain_search(TestStats *stats)
{
    uint64_t rng = 0x0F1E2D3C4B5A6978ULL;
    GameState state;
    game_init(&state);

    for (int ply = 0; ply < 20; ++ply) {
        play_random(&state, &rng, 1);
        if (state.result != GAME_RESULT_ONGOING
            || game_must_pass(&state)) {
            continue;
        }

        AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0);
        config.time_budget_ms = 10000;
        config.max_depth = 4;

        Move with_tt = {0, 0};
        Move plain = {0, 0};
        AiStats tt_stats;
        AiStats plain_stats;
        const bool found_tt = ai_search_choose_ex(
            &state, &config, true, &with_tt, &tt_stats);
        const bool found_plain = ai_search_choose_ex(
            &state, &config, false, &plain, &plain_stats);

        REVERSI_TEST_ASSERT(stats, found_tt == found_plain);
        if (!found_tt || !found_plain) {
            continue;
        }
        REVERSI_TEST_ASSERT(stats, tt_stats.score == plain_stats.score);
        REVERSI_TEST_ASSERT(stats, tt_stats.depth == plain_stats.depth);
        REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
            &state.board, state.current_player, with_tt));
        REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
            &state.board, state.current_player, plain));
    }
}

static void test_single_and_pass_positions(TestStats *stats)
{
    GameState state;
    build_one_move_white(&state);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&state.board, STONE_WHITE) == 1);

    const AiDifficulty difficulties[3] = {
        AI_DIFFICULTY_EASY, AI_DIFFICULTY_MEDIUM, AI_DIFFICULTY_HARD
    };
    for (int i = 0; i < 3; ++i) {
        AiConfig config = ai_default_config(difficulties[i], 3);
        if (difficulties[i] != AI_DIFFICULTY_EASY) {
            config.time_budget_ms = 10000;
            config.max_depth = 4;
        }
        Move move = {0, 0};
        AiStats search;
        REVERSI_TEST_ASSERT(stats,
            ai_choose_move(&state, &config, &move, &search));
        REVERSI_TEST_ASSERT(stats, move.row == 0 && move.col == 2);
    }

    /* The auto-pass fixture from core.match: after Black plays a1, White has
       no legal move. */
    board_clear(&state.board);
    (void)board_set(&state.board, 0, 2, STONE_BLACK);
    (void)board_set(&state.board, 0, 1, STONE_WHITE);
    (void)board_set(&state.board, 3, 4, STONE_WHITE);
    (void)board_set(&state.board, 3, 5, STONE_BLACK);
    (void)board_set(&state.board, 3, 6, STONE_BLACK);
    (void)board_set(&state.board, 3, 7, STONE_BLACK);
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_ONGOING;
    REVERSI_TEST_ASSERT(stats,
        game_apply_move(&state, (Move){.row = 0, .col = 0}));
    state.current_player = STONE_WHITE;
    state.result = GAME_RESULT_ONGOING;
    REVERSI_TEST_ASSERT(stats, game_must_pass(&state));
    AiConfig config = ai_default_config(AI_DIFFICULTY_MEDIUM, 0);
    config.time_budget_ms = 10000;
    config.max_depth = 4;
    Move move = {0, 0};
    REVERSI_TEST_ASSERT(stats,
        !ai_choose_move(&state, &config, &move, NULL));
}

static bool always_cancel(void *context)
{
    (void)context;
    return true;
}

static void test_invalid_config_rejected(TestStats *stats)
{
    GameState opening;
    game_init(&opening);
    Move move = {0, 0};
    AiConfig invalid = ai_default_config(AI_DIFFICULTY_MEDIUM, 0);
    invalid.max_depth = 0;
    REVERSI_TEST_ASSERT(stats,
        !ai_search_choose_ex(&opening, &invalid, false, &move, NULL));
    REVERSI_TEST_ASSERT(stats,
        !ai_easy_choose(&opening, &invalid, &move, NULL));
}

static void test_cancel_and_determinism(TestStats *stats)
{
    GameState state;
    game_init(&state);
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0);
    config.should_cancel = always_cancel;
    Move move = {0, 0};
    AiStats search;
    REVERSI_TEST_ASSERT(stats,
        !ai_choose_move(&state, &config, &move, &search));
    REVERSI_TEST_ASSERT(stats, search.cancelled);
    REVERSI_TEST_ASSERT(stats, search.depth == 0);

    config = ai_default_config(AI_DIFFICULTY_MEDIUM, 5);
    config.time_budget_ms = 10000;
    config.max_depth = 4;
    Move first = {0, 0};
    Move second = {0, 0};
    REVERSI_TEST_ASSERT(stats,
        ai_choose_move(&state, &config, &first, NULL));
    REVERSI_TEST_ASSERT(stats,
        ai_choose_move(&state, &config, &second, NULL));
    REVERSI_TEST_ASSERT(stats,
        first.row == second.row && first.col == second.col);

    config = ai_default_config(AI_DIFFICULTY_EASY, 9);
    REVERSI_TEST_ASSERT(stats,
        ai_choose_move(&state, &config, &first, NULL));
    REVERSI_TEST_ASSERT(stats,
        ai_choose_move(&state, &config, &second, NULL));
    REVERSI_TEST_ASSERT(stats,
        first.row == second.row && first.col == second.col);
}

static void run_pve_game(TestStats *stats, Stone human,
                         AiDifficulty difficulty, bool use_defaults)
{
    const MatchConfig config = {
        .mode = GAME_MODE_PVE,
        .human_color = human,
        .ai = ai_controller_for_difficulty(difficulty)
    };
    Match match;
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    AiConfig ai = ai_default_config(difficulty, 0xC0FFEEu);
    if (!use_defaults) {
        /* The matrix games stay deterministic and fast; the benchmark and
           the dedicated default-Hard game exercise the shipping budgets. */
        if (difficulty == AI_DIFFICULTY_HARD) {
            ai.max_depth = 4;
            ai.time_budget_ms = 10000;
        } else if (difficulty == AI_DIFFICULTY_MEDIUM) {
            ai.time_budget_ms = 10000;
        }
    }

    int human_moves = 0;
    int ai_moves = 0;
    int guard = 0;

    while (match.phase != MATCH_PHASE_GAME_OVER && guard++ < 1000) {
        if (match.phase == MATCH_PHASE_PLAYER_TURN) {
            if (match_is_human_turn(&match)) {
                Move legal[REVERSI_BOARD_CELL_COUNT];
                const int count = rules_generate_legal_moves(
                    &match.state.board, match.state.current_player, legal,
                    REVERSI_BOARD_CELL_COUNT);
                REVERSI_TEST_ASSERT(stats, count > 0);
                REVERSI_TEST_ASSERT(stats,
                    match_apply_move(&match, legal[0]));
                ++human_moves;
            } else {
                REVERSI_TEST_ASSERT(stats, match_begin_ai_turn(&match));
                Move move = {0, 0};
                REVERSI_TEST_ASSERT(stats,
                    ai_choose_move(&match.state, &ai, &move, NULL));
                REVERSI_TEST_ASSERT(stats,
                    rules_is_legal_move(&match.state.board,
                                        match.state.current_player, move));
                REVERSI_TEST_ASSERT(stats, match_apply_move(&match, move));
                ++ai_moves;
            }
        } else if (match.phase == MATCH_PHASE_ANIMATING) {
            REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
        } else if (match.phase == MATCH_PHASE_PASSING_TURN) {
            REVERSI_TEST_ASSERT(stats, match_finish_pass(&match));
        } else {
            REVERSI_TEST_ASSERT(stats, false);
            break;
        }
    }

    REVERSI_TEST_ASSERT(stats, guard < 1000);
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_GAME_OVER);
    REVERSI_TEST_ASSERT(stats, match.state.result != GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, human_moves > 0);
    REVERSI_TEST_ASSERT(stats, ai_moves > 0);
    REVERSI_TEST_ASSERT(stats,
        board_count(&match.state.board, STONE_EMPTY)
        == REVERSI_BOARD_CELL_COUNT - 4 - human_moves - ai_moves);
}

static void test_pve_full_games(TestStats *stats)
{
    run_pve_game(stats, STONE_BLACK, AI_DIFFICULTY_EASY, false);
    run_pve_game(stats, STONE_WHITE, AI_DIFFICULTY_EASY, false);
    run_pve_game(stats, STONE_BLACK, AI_DIFFICULTY_MEDIUM, false);
    run_pve_game(stats, STONE_WHITE, AI_DIFFICULTY_MEDIUM, false);
    run_pve_game(stats, STONE_BLACK, AI_DIFFICULTY_HARD, false);
    run_pve_game(stats, STONE_WHITE, AI_DIFFICULTY_HARD, false);
    /* One full game with the shipping Hard depth and 500 ms budget. */
    run_pve_game(stats, STONE_BLACK, AI_DIFFICULTY_HARD, true);
}

static void test_pve_undo_during_thinking(TestStats *stats)
{
    const MatchConfig config = {
        .mode = GAME_MODE_PVE,
        .human_color = STONE_BLACK,
        .ai = ai_controller_for_difficulty(AI_DIFFICULTY_MEDIUM)
    };
    Match match;
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 5, .col = 3}));
    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_begin_ai_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_cancel_ai_turn(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_count(&match.state.board, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats,
        board_count(&match.state.board, STONE_WHITE) == 2);
    REVERSI_TEST_ASSERT(stats, match.state.result == GAME_RESULT_ONGOING);

    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 5, .col = 3}));
    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match_begin_ai_turn(&match));
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_BLACK);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_config_and_names);
    REVERSI_RUN_TEST(&stats, test_evaluation);
    REVERSI_RUN_TEST(&stats, test_difficulty_evaluation);
    REVERSI_RUN_TEST(&stats, test_search_returns_legal_moves);
    REVERSI_RUN_TEST(&stats, test_search_many_positions);
    REVERSI_RUN_TEST(&stats, test_tt_matches_plain_search);
    REVERSI_RUN_TEST(&stats, test_single_and_pass_positions);
    REVERSI_RUN_TEST(&stats, test_invalid_config_rejected);
    REVERSI_RUN_TEST(&stats, test_cancel_and_determinism);
    REVERSI_RUN_TEST(&stats, test_pve_full_games);
    REVERSI_RUN_TEST(&stats, test_pve_undo_during_thinking);

    return reversi_test_summary(&stats, "ai.core");
}
