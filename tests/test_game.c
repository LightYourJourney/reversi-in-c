#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

static GameState make_state(const Board *board, Stone current,
                            GameResult result)
{
    GameState state;
    state.board = *board;
    state.current_player = current;
    state.result = result;
    return state;
}

static void build_auto_pass_start(Board *board)
{
    board_clear(board);
    (void)board_set(board, 0, 2, STONE_BLACK);
    (void)board_set(board, 0, 1, STONE_WHITE);
    (void)board_set(board, 3, 4, STONE_WHITE);
    (void)board_set(board, 3, 5, STONE_BLACK);
    (void)board_set(board, 3, 6, STONE_BLACK);
    (void)board_set(board, 3, 7, STONE_BLACK);
}

static void fill_full_board(Board *board, int black_extra)
{
    board_clear(board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = ((row + col) % 2 == 0)
                ? STONE_BLACK : STONE_WHITE;
            (void)board_set(board, row, col, stone);
        }
    }

    int remaining = black_extra < 0 ? -black_extra : black_extra;
    for (int row = 0; row < REVERSI_BOARD_SIZE && remaining > 0; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE && remaining > 0; ++col) {
            const Stone current = board_get(board, row, col);
            if (black_extra > 0 && current == STONE_WHITE) {
                (void)board_set(board, row, col, STONE_BLACK);
                --remaining;
            } else if (black_extra < 0 && current == STONE_BLACK) {
                (void)board_set(board, row, col, STONE_WHITE);
                --remaining;
            }
        }
    }
}

static void test_init(TestStats *stats)
{
    GameState state;
    game_init(&state);

    REVERSI_TEST_ASSERT(stats, state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_WHITE) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_EMPTY) == 60);
    REVERSI_TEST_ASSERT(stats, !game_is_over(&state));
    REVERSI_TEST_ASSERT(stats, game_result(&state) == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, !game_must_pass(&state));
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));

    GameState before = state;
    REVERSI_TEST_ASSERT(stats, !game_pass(&state));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));

    game_init(NULL);
    REVERSI_TEST_ASSERT(stats, !game_is_over(NULL));
    REVERSI_TEST_ASSERT(stats, game_result(NULL) == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, !game_must_pass(NULL));
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(NULL));
    REVERSI_TEST_ASSERT(stats, !game_apply_move(NULL, (Move){0, 0}));
    REVERSI_TEST_ASSERT(stats, !game_pass(NULL));
}

static void test_legal_move(TestStats *stats)
{
    GameState state;
    game_init(&state);

    Move c4 = {-1, -1};
    Move d4 = {-1, -1};
    REVERSI_TEST_ASSERT(stats, move_from_notation("c4", &c4));
    REVERSI_TEST_ASSERT(stats, move_from_notation("d4", &d4));

    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, c4));
    REVERSI_TEST_ASSERT(stats, state.current_player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, board_get(&state.board, c4.row, c4.col)
                               == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&state.board, d4.row, d4.col)
                               == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_BLACK) == 4);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_WHITE) == 1);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, !game_is_over(&state));
}

static void test_illegal_moves(TestStats *stats)
{
    GameState state;
    game_init(&state);

    GameState before = state;
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){3, 3}));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));

    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){-1, 0}));
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){8, 0}));
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){0, -1}));
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){0, 8}));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));

    state.current_player = STONE_EMPTY;
    before = state;
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){4, 2}));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));
}

static void test_auto_pass(TestStats *stats)
{
    Board board;
    build_auto_pass_start(&board);
    GameState state = make_state(&board, STONE_BLACK, GAME_RESULT_ONGOING);

    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, (Move){0, 0}));

    REVERSI_TEST_ASSERT(stats, state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats,
        board_get(&state.board, 0, 0) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_get(&state.board, 0, 1) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&state.board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&state.board, STONE_BLACK) > 0);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, !game_is_over(&state));
}

static void test_explicit_pass(TestStats *stats)
{
    Board board;
    build_auto_pass_start(&board);
    GameState state = make_state(&board, STONE_BLACK, GAME_RESULT_ONGOING);

    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, (Move){0, 0}));
    state.current_player = STONE_WHITE;

    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, game_must_pass(&state));

    REVERSI_TEST_ASSERT(stats, game_pass(&state));
    REVERSI_TEST_ASSERT(stats, state.current_player == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));

    GameState before = state;
    REVERSI_TEST_ASSERT(stats, !game_pass(&state));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));
}

static void test_both_no_moves_terminal(TestStats *stats)
{
    Board board;
    board_clear(&board);
    (void)board_set(&board, 0, 1, STONE_WHITE);
    (void)board_set(&board, 0, 2, STONE_BLACK);

    GameState state = make_state(&board, STONE_BLACK, GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, (Move){0, 0}));

    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, game_result(&state) == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, game_is_over(&state));
    REVERSI_TEST_ASSERT(stats, !board_is_full(&state.board));
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_BLACK) == 3);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&state.board, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&state.board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
}

static void test_full_board_results(TestStats *stats)
{
    struct {
        int black_extra;
        GameResult expected;
        int black;
        int white;
    } cases[] = {
        {0, GAME_RESULT_DRAW, 32, 32},
        {1, GAME_RESULT_BLACK_WIN, 33, 31},
        {-1, GAME_RESULT_WHITE_WIN, 31, 33}
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Board board;
        fill_full_board(&board, cases[i].black_extra);

        GameState state = make_state(&board, STONE_BLACK,
                                     GAME_RESULT_ONGOING);
        REVERSI_TEST_ASSERT(stats, board_is_full(&state.board));
        REVERSI_TEST_ASSERT(stats, game_must_pass(&state));
        REVERSI_TEST_ASSERT(stats, !game_is_consistent(&state));

        REVERSI_TEST_ASSERT(stats, game_pass(&state));
        REVERSI_TEST_ASSERT(stats, state.result == cases[i].expected);
        REVERSI_TEST_ASSERT(stats, game_is_over(&state));
        REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
        REVERSI_TEST_ASSERT(stats,
            board_count(&state.board, STONE_BLACK) == cases[i].black);
        REVERSI_TEST_ASSERT(stats,
            board_count(&state.board, STONE_WHITE) == cases[i].white);
    }

    Board board;
    fill_full_board(&board, 0);
    GameState wrong = make_state(&board, STONE_BLACK,
                                 GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&wrong));
}

static void test_last_move_fills_board(TestStats *stats)
{
    Board board;
    board_clear(&board);
    (void)board_set(&board, 0, 1, STONE_WHITE);
    (void)board_set(&board, 0, 2, STONE_BLACK);

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (row == 0 && col <= 2) {
                continue;
            }
            (void)board_set(&board, row, col, STONE_WHITE);
        }
    }

    GameState state = make_state(&board, STONE_BLACK, GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, (Move){0, 0}));

    REVERSI_TEST_ASSERT(stats, board_is_full(&state.board));
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, game_is_over(&state));
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_BLACK) == 3);
    REVERSI_TEST_ASSERT(stats, board_count(&state.board, STONE_WHITE) == 61);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
}

static void test_consistency_checks(TestStats *stats)
{
    GameState state;
    game_init(&state);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));

    state.current_player = STONE_EMPTY;
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&state));

    state.current_player = STONE_BLACK;
    state.board.cells[0][0] = (Stone)99;
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&state));

    state.board.cells[0][0] = STONE_EMPTY;
    state.result = GAME_RESULT_BLACK_WIN;
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&state));

    Board terminal;
    board_clear(&terminal);
    (void)board_set(&terminal, 0, 0, STONE_BLACK);
    GameState no_moves = make_state(&terminal, STONE_BLACK,
                                    GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&no_moves));

    no_moves.result = GAME_RESULT_BLACK_WIN;
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&no_moves));

    no_moves.result = GAME_RESULT_DRAW;
    REVERSI_TEST_ASSERT(stats, !game_is_consistent(&no_moves));
}

static void test_after_game_over(TestStats *stats)
{
    Board board;
    fill_full_board(&board, 0);
    GameState state = make_state(&board, STONE_BLACK, GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, game_pass(&state));
    REVERSI_TEST_ASSERT(stats, game_is_over(&state));

    GameState before = state;
    REVERSI_TEST_ASSERT(stats, !game_apply_move(&state, (Move){0, 0}));
    REVERSI_TEST_ASSERT(stats, !game_pass(&state));
    REVERSI_TEST_ASSERT(stats, game_states_equal(&state, &before));
}

int main(void)
{
    TestStats stats = {0, 0};

    REVERSI_RUN_TEST(&stats, test_init);
    REVERSI_RUN_TEST(&stats, test_legal_move);
    REVERSI_RUN_TEST(&stats, test_illegal_moves);
    REVERSI_RUN_TEST(&stats, test_auto_pass);
    REVERSI_RUN_TEST(&stats, test_explicit_pass);
    REVERSI_RUN_TEST(&stats, test_both_no_moves_terminal);
    REVERSI_RUN_TEST(&stats, test_full_board_results);
    REVERSI_RUN_TEST(&stats, test_last_move_fills_board);
    REVERSI_RUN_TEST(&stats, test_consistency_checks);
    REVERSI_RUN_TEST(&stats, test_after_game_over);

    return reversi_test_summary(&stats, "core.game");
}
