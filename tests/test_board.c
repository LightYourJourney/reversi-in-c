#include "core/board.h"
#include "test_harness.h"

static void test_initial_position(TestStats *stats)
{
    Board board;
    board_init(&board);

    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 60);

    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 3) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 4, 4) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 3, 4) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 4, 3) == STONE_BLACK);

    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 7, 7) == STONE_EMPTY);
}

static void test_set_and_get(TestStats *stats)
{
    Board board;
    board_init(&board);

    board_set(&board, 0, 0, STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 3);

    board_set(&board, 0, 0, STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_WHITE) == 3);

    board_set(&board, 0, 0, STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_get(&board, 0, 0) == STONE_EMPTY);
    REVERSI_TEST_ASSERT(stats, board_count(&board, STONE_EMPTY) == 60);
}

static void test_bounds(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats, board_in_bounds(0, 0));
    REVERSI_TEST_ASSERT(stats, board_in_bounds(0, REVERSI_BOARD_SIZE - 1));
    REVERSI_TEST_ASSERT(stats, board_in_bounds(REVERSI_BOARD_SIZE - 1, 0));
    REVERSI_TEST_ASSERT(stats, board_in_bounds(REVERSI_BOARD_SIZE - 1,
                                               REVERSI_BOARD_SIZE - 1));

    REVERSI_TEST_ASSERT(stats, !board_in_bounds(-1, 0));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(0, -1));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(REVERSI_BOARD_SIZE, 0));
    REVERSI_TEST_ASSERT(stats, !board_in_bounds(0, REVERSI_BOARD_SIZE));
}

int main(void)
{
    TestStats stats = {0, 0};

    REVERSI_RUN_TEST(&stats, test_initial_position);
    REVERSI_RUN_TEST(&stats, test_set_and_get);
    REVERSI_RUN_TEST(&stats, test_bounds);

    return reversi_test_summary(&stats, "core.board");
}
