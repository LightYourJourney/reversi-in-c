#include "core/board.h"

#include <assert.h>
#include <stddef.h>

bool board_in_bounds(int row, int col)
{
    return row >= 0 && row < REVERSI_BOARD_SIZE
        && col >= 0 && col < REVERSI_BOARD_SIZE;
}

Stone board_get(const Board *board, int row, int col)
{
    assert(board != NULL);
    assert(board_in_bounds(row, col));
    return board->cells[row][col];
}

void board_set(Board *board, int row, int col, Stone stone)
{
    assert(board != NULL);
    assert(board_in_bounds(row, col));
    assert(stone == STONE_EMPTY || stone == STONE_BLACK || stone == STONE_WHITE);
    board->cells[row][col] = stone;
}

void board_init(Board *board)
{
    assert(board != NULL);

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            board->cells[row][col] = STONE_EMPTY;
        }
    }

    /* Standard opening: white on d4/e5, black on e4/d5. */
    board->cells[3][3] = STONE_WHITE;
    board->cells[4][4] = STONE_WHITE;
    board->cells[3][4] = STONE_BLACK;
    board->cells[4][3] = STONE_BLACK;
}

int board_count(const Board *board, Stone stone)
{
    assert(board != NULL);

    int count = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board->cells[row][col] == stone) {
                ++count;
            }
        }
    }
    return count;
}
