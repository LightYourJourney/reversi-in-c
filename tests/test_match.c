#include "core/game.h"
#include "core/match.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

static bool stub_select_move(void *context, const GameState *state,
                             Move *out_move)
{
    (void)context;
    (void)state;
    (void)out_move;
    return false;
}

static MatchConfig pvp_config(void)
{
    return (MatchConfig){
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
}

static MatchConfig pve_config(Stone human_color)
{
    static const AiController stub = {
        .name = "stub",
        .select_move = stub_select_move,
        .context = NULL
    };
    return (MatchConfig){
        .mode = GAME_MODE_PVE,
        .human_color = human_color,
        .ai = &stub
    };
}

static void finish_phases(TestStats *stats, Match *match)
{
    if (match->phase == MATCH_PHASE_ANIMATING) {
        REVERSI_TEST_ASSERT(stats, match_finish_animation(match));
    }
    if (match->phase == MATCH_PHASE_PASSING_TURN) {
        REVERSI_TEST_ASSERT(stats, match_finish_pass(match));
    }
    REVERSI_TEST_ASSERT(stats, match_is_consistent(match));
}

static void play_move(TestStats *stats, Match *match, Move move)
{
    REVERSI_TEST_ASSERT(stats, match_apply_move(match, move));
    REVERSI_TEST_ASSERT(stats, match->phase == MATCH_PHASE_ANIMATING);
    finish_phases(stats, match);
}

static Move first_legal_move(const Match *match)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &match->state.board, match->state.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    return count > 0 ? moves[0] : (Move){0, 0};
}

static bool play_to_game_over(TestStats *stats, Match *match)
{
    int guard = 0;
    while (match->phase != MATCH_PHASE_GAME_OVER && guard < 100) {
        if (match->phase == MATCH_PHASE_PLAYER_TURN) {
            REVERSI_TEST_ASSERT(stats,
                match_apply_move(match, first_legal_move(match)));
        }
        finish_phases(stats, match);
        ++guard;
    }
    REVERSI_TEST_ASSERT(stats, guard < 100);
    return match->phase == MATCH_PHASE_GAME_OVER;
}

static void test_start_and_config(TestStats *stats)
{
    Match match;
    GameState opening;
    game_init(&opening);

    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match_is_active(&match));
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats,
                        game_states_equal(&match.state, &opening));

    REVERSI_TEST_ASSERT(stats, !match_start(NULL, &config));
    REVERSI_TEST_ASSERT(stats, !match_start(&match, NULL));
    config.mode = (GameMode)99;
    REVERSI_TEST_ASSERT(stats, !match_start(&match, &config));
    config = pvp_config();
    config.human_color = STONE_EMPTY;
    REVERSI_TEST_ASSERT(stats, !match_start(&match, &config));
    config = pve_config(STONE_BLACK);
    config.ai = NULL;
    REVERSI_TEST_ASSERT(stats, !match_start(&match, &config));
}

static void test_move_undo_redo(TestStats *stats)
{
    Match match;
    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    const GameState opening = match.state;
    play_move(stats, &match, (Move){5, 3}); /* d3 */
    const GameState after_d3 = match.state;

    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, match.has_last_move);
    REVERSI_TEST_ASSERT(stats, match.last_move.row == 5
                        && match.last_move.col == 3);
    REVERSI_TEST_ASSERT(stats, match.last_flips == 1);
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, game_states_equal(&match.state, &opening));
    REVERSI_TEST_ASSERT(stats, !match.has_last_move);
    REVERSI_TEST_ASSERT(stats, match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&match.state, &after_d3));
    REVERSI_TEST_ASSERT(stats, match.has_last_move);
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));

    play_move(stats, &match, (Move){3, 2}); /* c5 */
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&match.state, &after_d3));
    REVERSI_TEST_ASSERT(stats, match_can_redo(&match));

    play_move(stats, &match, (Move){5, 4}); /* e3 replaces the redo branch */
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 3);
    REVERSI_TEST_ASSERT(stats, match.last_move.row == 5
                        && match.last_move.col == 4);
}

static void test_phase_gating_and_pause(TestStats *stats)
{
    Match match;
    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    REVERSI_TEST_ASSERT(stats, match_apply_move(&match, (Move){5, 3}));
    REVERSI_TEST_ASSERT(stats, !match_apply_move(&match, (Move){3, 2}));
    REVERSI_TEST_ASSERT(stats, !match_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_redo(&match));
    REVERSI_TEST_ASSERT(stats, !match_pause(&match));
    REVERSI_TEST_ASSERT(stats, !match_resume(&match));
    REVERSI_TEST_ASSERT(stats, !match_restart(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, !match_finish_animation(&match));

    REVERSI_TEST_ASSERT(stats, match_pause(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PAUSED);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats, !match_apply_move(&match, (Move){3, 2}));
    REVERSI_TEST_ASSERT(stats, !match_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_pause(&match));

    REVERSI_TEST_ASSERT(stats, match_resume(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats, !match_resume(&match));

    REVERSI_TEST_ASSERT(stats, match_pause(&match));
    REVERSI_TEST_ASSERT(stats, match_restart(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

static void build_auto_pass(GameState *state)
{
    board_clear(&state->board);
    (void)board_set(&state->board, 0, 2, STONE_BLACK);
    (void)board_set(&state->board, 0, 1, STONE_WHITE);
    (void)board_set(&state->board, 3, 4, STONE_WHITE);
    (void)board_set(&state->board, 3, 5, STONE_BLACK);
    (void)board_set(&state->board, 3, 6, STONE_BLACK);
    (void)board_set(&state->board, 3, 7, STONE_BLACK);
    state->current_player = STONE_BLACK;
    state->result = GAME_RESULT_ONGOING;
}

static void test_auto_pass_and_undo(TestStats *stats)
{
    GameState start;
    build_auto_pass(&start);
    Match match;
    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start_from(&match, &config, &start));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_apply_move(&match, (Move){0, 0}));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_ANIMATING);
    REVERSI_TEST_ASSERT(stats, match.last_pass.occurred);
    REVERSI_TEST_ASSERT(stats, match.last_pass.player == STONE_WHITE);

    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PASSING_TURN);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats, !match_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_pause(&match));

    REVERSI_TEST_ASSERT(stats, match_finish_pass(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&match.state, &start));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match_can_redo(&match));

    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match.last_pass.occurred);
    REVERSI_TEST_ASSERT(stats, match.last_pass.player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

static void test_terminal_and_restart(TestStats *stats)
{
    Match match;
    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    REVERSI_TEST_ASSERT(stats, play_to_game_over(stats, &match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_GAME_OVER);
    REVERSI_TEST_ASSERT(stats, match.state.result != GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, !match_is_active(&match));
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_restart(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats, play_to_game_over(stats, &match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, match.state.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_GAME_OVER);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);

    REVERSI_TEST_ASSERT(stats, match_restart(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));

    play_move(stats, &match, (Move){5, 3});
    REVERSI_TEST_ASSERT(stats, match_restart(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

static void test_mode_switch_and_pve_undo(TestStats *stats)
{
    Match match;
    MatchConfig pvp = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &pvp));
    play_move(stats, &match, (Move){5, 3});

    MatchConfig pve = pve_config(STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, match_start(&match, &pve));
    REVERSI_TEST_ASSERT(stats, match.config.mode == GAME_MODE_PVE);
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, history_count(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    play_move(stats, &match, (Move){5, 3}); /* human (Black) */
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    play_move(stats, &match, first_legal_move(&match)); /* AI (White) */
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 2);
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 0);
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 2);
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    MatchConfig white = pve_config(STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, match_start(&match, &white));
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));

    play_move(stats, &match, first_legal_move(&match)); /* AI Black */
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, !match_undo(&match));

    play_move(stats, &match, first_legal_move(&match)); /* human White */
    play_move(stats, &match, first_legal_move(&match)); /* AI Black */
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));
    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.state.current_player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

static void test_terminal_without_full_board(TestStats *stats)
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

    GameState initial;
    game_init(&initial);
    board_clear(&initial.board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const char cell = rows[row][col];
            const Stone stone = cell == 'B' ? STONE_BLACK
                : (cell == 'W' ? STONE_WHITE : STONE_EMPTY);
            REVERSI_TEST_ASSERT(stats,
                board_set(&initial.board, row, col, stone));
        }
    }
    initial.current_player = STONE_WHITE;
    initial.result = GAME_RESULT_ONGOING;
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&initial));

    Match match;
    MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats,
        match_start_from(&match, &config, &initial));

    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 0, .col = 2}));
    REVERSI_TEST_ASSERT(stats, match.state.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, !match.last_pass.occurred);
    REVERSI_TEST_ASSERT(stats, !board_is_full(&match.state.board));
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&match.state.board, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_GAME_OVER);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, game_states_equal(&match.state, &initial));
    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_GAME_OVER);
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

static void test_pve_redo_branch_end(TestStats *stats)
{
    Match match;
    MatchConfig pve = pve_config(STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, match_start(&match, &pve));

    play_move(stats, &match, (Move){5, 3}); /* human move; AI to move */
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match_can_undo(&match));

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 0);
    REVERSI_TEST_ASSERT(stats, match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_can_redo(&match));

    REVERSI_TEST_ASSERT(stats, match_redo(&match));
    REVERSI_TEST_ASSERT(stats, history_position(&match.history) == 1);
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, !match_can_redo(&match));
    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_start_and_config);
    REVERSI_RUN_TEST(&stats, test_move_undo_redo);
    REVERSI_RUN_TEST(&stats, test_phase_gating_and_pause);
    REVERSI_RUN_TEST(&stats, test_auto_pass_and_undo);
    REVERSI_RUN_TEST(&stats, test_terminal_and_restart);
    REVERSI_RUN_TEST(&stats, test_mode_switch_and_pve_undo);
    REVERSI_RUN_TEST(&stats, test_pve_redo_branch_end);
    REVERSI_RUN_TEST(&stats, test_terminal_without_full_board);

    return reversi_test_summary(&stats, "core.match");
}
