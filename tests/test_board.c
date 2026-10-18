#include "core/board.h"
#include "test_harness.h"

#include <string.h>

static void test_initial_position(TestStats *stats)
{
    Board board;
    board_init(&board);

    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 60);
    REVERSI_TEST_ASSERT(stats, board_is_valid(&board));
    REVERSI_TEST_ASSERT(stats, !board_is_full(&board));

    Move d4 = {-1, -1};
    REVERSI_TEST_ASSERT(stats, move_from_notation("d4", &d4));
    REVERSI_TEST_ASSERT(stats, d4.row == 4 && d4.col == 3);

    const char *white_cells[] = {"d4", "e5"};
    for (size_t i = 0; i < sizeof(white_cells) / sizeof(white_cells[0]); ++i) {
        Move move = {-1, -1};
        REVERSI_TEST_ASSERT(stats, move_from_notation(white_cells[i], &move));
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, move.row, move.col) == STONE_WHITE);
    }

    const char *black_cells[] = {"d5", "e4"};
    for (size_t i = 0; i < sizeof(black_cells) / sizeof(black_cells[0]); ++i) {
        Move move = {-1, -1};
        REVERSI_TEST_ASSERT(stats, move_from_notation(black_cells[i], &move));
        REVERSI_TEST_ASSERT(stats,
            board_get(&board, move.row, move.col) == STONE_BLACK);
    }
}

static void test_clear_set_and_counts(TestStats *stats)
{
    Board board;
    board_init(&board);
    board_clear(&board);

    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 64);
    REVERSI_TEST_ASSERT(stats, board_is_valid(&board));
    REVERSI_TEST_ASSERT(stats, !board_is_full(&board));

    REVERSI_TEST_ASSERT(stats, board_set(&board, 0, 0, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 1);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 63);

    REVERSI_TEST_ASSERT(stats, board_set(&board, 0, 0, STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 1);

    REVERSI_TEST_ASSERT(stats, board_set(&board, 0, 0, STONE_EMPTY));
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 64);

    REVERSI_TEST_ASSERT(stats, board_set(&board, 1, 1, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !board_set(&board, 1, 1, (Stone)99));
    REVERSI_TEST_ASSERT(stats, board_get(&board, 1, 1) == STONE_BLACK);

    REVERSI_TEST_ASSERT(stats, !board_set(NULL, 0, 0, STONE_BLACK));
    board_clear(NULL);
}

static void test_bounds_and_safe_access(TestStats *stats)
{
    Board board;
    board_init(&board);

    REVERSI_TEST_ASSERT(stats, board_in_bounds(0, 0));
    REVERSI_TEST_ASSERT(stats, board_in_bounds(7, 7));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(-1, 0));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(0, -1));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(8, 0));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(0, 8));

    REVERSI_TEST_ASSERT(stats, board_get(NULL, 0, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, -1, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, -1) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 8, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 8) == STONE_EMPTY);

    const int black_before = board_count(&board, STONE_BLACK);
    const int white_before = board_count(&board, STONE_WHITE);
    const int empty_before = board_count(&board, STONE_EMPTY);

    REVERSI_TEST_ASSERT(stats, !board_set(&board, -1, 0, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !board_set(&board, 0, -1, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !board_set(&board, 8, 0, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, !board_set(&board, 0, 8, STONE_BLACK));

    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == black_before);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == white_before);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == empty_before);

    REVERSI_TEST_ASSERT(stats, board_count(NULL, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats, !board_is_full(NULL));
    REVERSI_TEST_ASSERT(stats, !board_is_valid(NULL));
}

static void test_notation(TestStats *stats)
{
    Move move = {-1, -1};
    REVERSI_TEST_ASSERT(stats, move_from_notation("d4", &move));
    REVERSI_TEST_ASSERT(stats, move.row == 4 && move.col == 3);

    char text[3] = {0};
    REVERSI_TEST_ASSERT(stats, move_to_notation(move, text));
    REVERSI_TEST_ASSERT(stats, strcmp(text, "d4") == 0);
    REVERSI_TEST_ASSERT(stats, text[2] == '\0');

    REVERSI_TEST_ASSERT(stats, move_from_notation("D4", &move));
    REVERSI_TEST_ASSERT(stats, move.row == 4 && move.col == 3);

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Move original = {.row = row, .col = col};
            char notation[3] = {0};
            REVERSI_TEST_ASSERT(stats, move_to_notation(original, notation));
            REVERSI_TEST_ASSERT(stats,
                notation[0] >= 'a' && notation[0] <= 'h');
            REVERSI_TEST_ASSERT(stats,
                notation[1] >= '1' && notation[1] <= '8');
            REVERSI_TEST_ASSERT(stats, notation[2] == '\0');

            Move parsed = {-1, -1};
            REVERSI_TEST_ASSERT(stats,
                move_from_notation(notation, &parsed));
            REVERSI_TEST_ASSERT(stats, parsed.row == row);
            REVERSI_TEST_ASSERT(stats, parsed.col == col);
        }
    }

    const char *invalid[] = {
        NULL, "", "a", "a1x", "i1", "a9", "1a", "aa", "d44"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Move unchanged = {7, 7};
        REVERSI_TEST_ASSERT(stats, !move_from_notation(invalid[i], &unchanged));
        REVERSI_TEST_ASSERT(stats, unchanged.row == 7);
        REVERSI_TEST_ASSERT(stats, unchanged.col == 7);
    }

    REVERSI_TEST_ASSERT(stats, !move_from_notation("d4", NULL));

    char untouched[3] = {'x', 'x', 'x'};
    REVERSI_TEST_ASSERT(stats, !move_to_notation((Move){-1, 0}, untouched));
    REVERSI_TEST_ASSERT(stats, untouched[0] == 'x');
    REVERSI_TEST_ASSERT(stats, !move_to_notation((Move){0, 8}, untouched));
    REVERSI_TEST_ASSERT(stats, !move_to_notation((Move){8, 0}, untouched));
    REVERSI_TEST_ASSERT(stats, !move_to_notation((Move){0, 0}, NULL));
}

static void test_full_and_valid(TestStats *stats)
{
    Board board;
    board_clear(&board);

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = ((row + col) % 2 == 0)
                ? STONE_BLACK : STONE_WHITE;
            REVERSI_TEST_ASSERT(stats, board_set(&board, row, col, stone));
        }
    }

    REVERSI_TEST_ASSERT(stats, board_is_full(&board));
    REVERSI_TEST_ASSERT(stats, board_is_valid(&board));
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 32);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 32);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 0);

    REVERSI_TEST_ASSERT(stats, board_set(&board, 0, 0, STONE_EMPTY));
    REVERSI_TEST_ASSERT(stats, !board_is_full(&board));

    board.cells[1][1] = (Stone)99;
    REVERSI_TEST_ASSERT(stats, !board_is_valid(&board));
    board.cells[1][1] = STONE_BLACK;
    REVERSI_TEST_ASSERT(stats, board_is_valid(&board));
}

static void test_opponent(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats, stone_opponent(STONE_BLACK) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, stone_opponent(STONE_WHITE) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, stone_opponent(STONE_EMPTY) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, stone_opponent((Stone)99) == STONE_EMPTY);

    REVERSI_TEST_ASSERT(stats, stone_is_player(STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, stone_is_player(STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, !stone_is_player(STONE_EMPTY));
    REVERSI_TEST_ASSERT(stats, !stone_is_player((Stone)99));
}

int main(void)
{
    TestStats stats = {0, 0};

    REVERSI_RUN_TEST(&stats, test_initial_position);
    REVERSI_RUN_TEST(&stats, test_clear_set_and_counts);
    REVERSI_RUN_TEST(&stats, test_bounds_and_safe_access);
    REVERSI_RUN_TEST(&stats, test_notation);
    REVERSI_RUN_TEST(&stats, test_full_and_valid);
    REVERSI_RUN_TEST(&stats, test_opponent);

    return reversi_test_summary(&stats, "core.board");
}
