#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

#include <stddef.h>

static const int DIRECTIONS[8][2] = {
    {-1, -1}, {-1, 0}, {-1, 1},
    { 0, -1},          { 0, 1},
    { 1, -1}, { 1, 0}, { 1, 1}
};

static Move notation_move(TestStats *stats, const char *text)
{
    Move move = {-1, -1};
    REVERSI_TEST_ASSERT(stats, move_from_notation(text, &move));
    return move;
}

static void test_opening_moves(TestStats *stats)
{
    Board board;
    board_init(&board);

    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_BLACK) == 4);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_WHITE) == 4);
    REVERSI_TEST_ASSERT(stats, !rules_must_pass(&board, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !rules_must_pass(&board, STONE_WHITE));

    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int written = rules_generate_legal_moves(
        &board, STONE_BLACK, moves, REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, written == 4);

    const char *black_expected[] = {"c4", "d3", "e6", "f5"};
    for (size_t i = 0;
         i < sizeof(black_expected) / sizeof(black_expected[0]); ++i) {
        const Move move = notation_move(stats, black_expected[i]);
        REVERSI_TEST_ASSERT(stats, move_in_list(moves, written, move));
    }

    const char *white_expected[] = {"c5", "d6", "e3", "f4"};
    for (size_t i = 0;
         i < sizeof(white_expected) / sizeof(white_expected[0]); ++i) {
        const Move move = notation_move(stats, white_expected[i]);
        REVERSI_TEST_ASSERT(stats,
            rules_is_legal_move(&board, STONE_WHITE, move));
    }

    for (int i = 0; i < written; ++i) {
        REVERSI_TEST_ASSERT(stats,
            rules_is_legal_move(&board, STONE_BLACK, moves[i]));
    }

    Move limited[2];
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, limited, 2) == 2);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, NULL, 64) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, limited, 0) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, limited, -1) == 0);
}

static void test_c4_flips_d4(TestStats *stats)
{
    Board board;
    board_init(&board);

    const Move c4 = notation_move(stats, "c4");
    const Move d4 = notation_move(stats, "d4");
    const Move e4 = notation_move(stats, "e4");
    const Move d5 = notation_move(stats, "d5");
    const Move e5 = notation_move(stats, "e5");

    REVERSI_TEST_ASSERT(stats,
        rules_is_legal_move(&board, STONE_BLACK, c4));
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, c4) == 1);

    REVERSI_TEST_ASSERT(stats,
        board_get(&board, c4.row, c4.col) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_get(&board, d4.row, d4.col) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_get(&board, e4.row, e4.col) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_get(&board, d5.row, d5.col) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        board_get(&board, e5.row, e5.col) == STONE_WHITE);

    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 4);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 1);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 59);
}

static void setup_single_direction(Board *board, Move move, int dr, int dc)
{
    board_clear(board);
    (void)board_set(board, move.row + dr, move.col + dc, STONE_WHITE);
    (void)board_set(board, move.row + 2 * dr, move.col + 2 * dc, STONE_BLACK);
}

static void test_eight_directions(TestStats *stats)
{
    const Move center = {3, 3};

    for (int i = 0; i < 8; ++i) {
        const int dr = DIRECTIONS[i][0];
        const int dc = DIRECTIONS[i][1];
        Board board;
        setup_single_direction(&board, center, dr, dc);

        REVERSI_TEST_ASSERT(stats,
            rules_is_legal_move(&board, STONE_BLACK, center));
        REVERSI_TEST_ASSERT(stats,
            rules_apply_move(&board, STONE_BLACK, center) == 1);

        REVERSI_TEST_ASSERT(stats,
            board_get(&board, center.row, center.col) == STONE_BLACK);
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, center.row + dr, center.col + dc)
                == STONE_BLACK);
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, center.row + 2 * dr, center.col + 2 * dc)
                == STONE_BLACK);
        REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 3);
        REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 0);
        REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 61);
    }
}

static void test_long_line(TestStats *stats)
{
    Board board;
    board_clear(&board);

    const Move move = {3, 3};
    for (int col = 4; col <= 6; ++col) {
        (void)board_set(&board, 3, col, STONE_WHITE);
    }
    (void)board_set(&board, 3, 7, STONE_BLACK);

    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == 3);
    for (int col = 3; col <= 7; ++col) {
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, 3, col) == STONE_BLACK);
    }
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 5);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 59);
}

static void test_multi_direction(TestStats *stats)
{
    Board board;
    board_clear(&board);

    const Move center = {3, 3};
    for (int i = 0; i < 8; ++i) {
        const int dr = DIRECTIONS[i][0];
        const int dc = DIRECTIONS[i][1];
        (void)board_set(&board, center.row + dr, center.col + dc,
                        STONE_WHITE);
        (void)board_set(&board, center.row + 2 * dr, center.col + 2 * dc,
                        STONE_BLACK);
    }

    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, center) == 8);

    for (int i = 0; i < 8; ++i) {
        REVERSI_TEST_ASSERT(stats,
            board_get(&board,
                      center.row + DIRECTIONS[i][0],
                      center.col + DIRECTIONS[i][1]) == STONE_BLACK);
    }
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 17);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 47);
}

static void test_mixed_direction_counts(TestStats *stats)
{
    Board board;
    board_clear(&board);

    const Move move = {3, 3};
    (void)board_set(&board, 3, 4, STONE_WHITE);
    (void)board_set(&board, 3, 5, STONE_BLACK);
    (void)board_set(&board, 4, 3, STONE_WHITE);
    (void)board_set(&board, 5, 3, STONE_WHITE);
    (void)board_set(&board, 6, 3, STONE_BLACK);

    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == 3);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 4) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 4, 3) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 5, 3) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 6);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 58);
}

static void test_gap_blocks_flip(TestStats *stats)
{
    Board board;
    board_clear(&board);

    const Move move = {3, 3};
    (void)board_set(&board, 3, 4, STONE_WHITE);
    (void)board_set(&board, 3, 6, STONE_BLACK);

    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, move));

    Board before = board;
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == -1);
    REVERSI_TEST_ASSERT(stats, boards_equal(&board, &before));
}

static void test_own_stone_blocks_flip(TestStats *stats)
{
    Board board;
    board_clear(&board);

    const Move move = {3, 3};
    (void)board_set(&board, 3, 4, STONE_BLACK);
    (void)board_set(&board, 3, 5, STONE_WHITE);
    (void)board_set(&board, 3, 6, STONE_BLACK);

    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, move));

    Board before = board;
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == -1);
    REVERSI_TEST_ASSERT(stats, boards_equal(&board, &before));

    /* An own stone terminates the run; opponents beyond it are untouched. */
    board_clear(&board);
    (void)board_set(&board, 3, 4, STONE_WHITE);
    (void)board_set(&board, 3, 5, STONE_BLACK);
    (void)board_set(&board, 3, 6, STONE_WHITE);
    (void)board_set(&board, 3, 7, STONE_BLACK);

    REVERSI_TEST_ASSERT(stats,
        rules_is_legal_move(&board, STONE_BLACK, move));
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == 1);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 3) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 4) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 5) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 6) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 7) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 4);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 1);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 59);
}

static void test_zero_flip_and_no_opponent(TestStats *stats)
{
    Board board;
    board_clear(&board);
    (void)board_set(&board, 3, 6, STONE_BLACK);

    const Move move = {3, 3};
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, move));
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, move) == -1);

    board_clear(&board);
    (void)board_set(&board, 0, 0, STONE_BLACK);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_WHITE) == 0);
    REVERSI_TEST_ASSERT(stats, rules_must_pass(&board, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, rules_must_pass(&board, STONE_WHITE));

    board_clear(&board);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_WHITE) == 0);
}

static void test_corners_and_edges(TestStats *stats)
{
    struct {
        const char *move;
        int dr;
        int dc;
    } cases[] = {
        {"a1", 0, 1},    /* bottom-left, horizontal right */
        {"h1", 0, -1},   /* bottom-right, horizontal left */
        {"a8", 1, 0},    /* top-left, vertical down */
        {"h8", 1, -1},   /* top-right, diagonal down-left */
        {"d1", 0, 1},    /* bottom edge, horizontal right */
        {"a4", 0, 1},    /* left edge, horizontal right */
        {"d8", 1, 0},    /* top edge, vertical down */
        {"h4", 0, -1}    /* right edge, horizontal left */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const Move move = notation_move(stats, cases[i].move);
        Board board;
        board_clear(&board);
        (void)board_set(&board,
                        move.row + cases[i].dr,
                        move.col + cases[i].dc,
                        STONE_WHITE);
        (void)board_set(&board,
                        move.row + 2 * cases[i].dr,
                        move.col + 2 * cases[i].dc,
                        STONE_BLACK);

        REVERSI_TEST_ASSERT(stats,
            rules_is_legal_move(&board, STONE_BLACK, move));
        REVERSI_TEST_ASSERT(stats,
            rules_apply_move(&board, STONE_BLACK, move) == 1);
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, move.row, move.col) == STONE_BLACK);
    }

    Board empty;
    board_clear(&empty);
    const char *corners[] = {"a1", "h1", "a8", "h8"};
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        const Move move = notation_move(stats, corners[i]);
        REVERSI_TEST_ASSERT(stats,
            !rules_is_legal_move(&empty, STONE_BLACK, move));
        REVERSI_TEST_ASSERT(stats,
            rules_apply_move(&empty, STONE_BLACK, move) == -1);
    }
}

static void test_invalid_inputs(TestStats *stats)
{
    Board board;
    board_init(&board);
    Board before = board;
    Move moves[REVERSI_BOARD_CELL_COUNT];

    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(NULL, STONE_BLACK, (Move){0, 0}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_EMPTY, (Move){4, 2}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, (Stone)99, (Move){4, 2}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, (Move){-1, 0}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, (Move){8, 0}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, (Move){0, -1}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, (Move){0, 8}));
    REVERSI_TEST_ASSERT(stats,
        !rules_is_legal_move(&board, STONE_BLACK, (Move){3, 3}));

    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(NULL, STONE_BLACK, (Move){0, 0}) == -1);
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_EMPTY, (Move){4, 2}) == -1);
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, (Stone)99, (Move){4, 2}) == -1);
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, (Move){-1, 0}) == -1);
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, (Move){8, 8}) == -1);
    REVERSI_TEST_ASSERT(stats,
        rules_apply_move(&board, STONE_BLACK, (Move){3, 3}) == -1);
    REVERSI_TEST_ASSERT(stats, boards_equal(&board, &before));

    REVERSI_TEST_ASSERT(stats, rules_count_legal_moves(NULL, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_count_legal_moves(&board, STONE_EMPTY) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(NULL, STONE_BLACK, moves,
                                   REVERSI_BOARD_CELL_COUNT) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_EMPTY, moves,
                                   REVERSI_BOARD_CELL_COUNT) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, NULL,
                                   REVERSI_BOARD_CELL_COUNT) == 0);
    REVERSI_TEST_ASSERT(stats,
        rules_generate_legal_moves(&board, STONE_BLACK, moves, 0) == 0);
    REVERSI_TEST_ASSERT(stats, !rules_must_pass(NULL, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !rules_must_pass(&board, STONE_EMPTY));
}

int main(void)
{
    TestStats stats = {0, 0};

    REVERSI_RUN_TEST(&stats, test_opening_moves);
    REVERSI_RUN_TEST(&stats, test_c4_flips_d4);
    REVERSI_RUN_TEST(&stats, test_eight_directions);
    REVERSI_RUN_TEST(&stats, test_long_line);
    REVERSI_RUN_TEST(&stats, test_multi_direction);
    REVERSI_RUN_TEST(&stats, test_mixed_direction_counts);
    REVERSI_RUN_TEST(&stats, test_gap_blocks_flip);
    REVERSI_RUN_TEST(&stats, test_own_stone_blocks_flip);
    REVERSI_RUN_TEST(&stats, test_zero_flip_and_no_opponent);
    REVERSI_RUN_TEST(&stats, test_corners_and_edges);
    REVERSI_RUN_TEST(&stats, test_invalid_inputs);

    return reversi_test_summary(&stats, "core.rules");
}
