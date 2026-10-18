#ifndef REVERSI_CORE_BOARD_H
#define REVERSI_CORE_BOARD_H

#include <stdbool.h>

#define REVERSI_BOARD_SIZE 8

/*
 * Coordinates use zero-based row/column indices.
 * Row 0 is the top edge and column 0 is the left edge.
 */
typedef enum Stone {
    STONE_EMPTY = 0,
    STONE_BLACK = 1,
    STONE_WHITE = 2
} Stone;

typedef struct Board {
    Stone cells[REVERSI_BOARD_SIZE][REVERSI_BOARD_SIZE];
} Board;

/* Returns true when (row, col) addresses a cell on the board. */
bool board_in_bounds(int row, int col);

/*
 * Cell accessors. get/set require in-bounds coordinates; violations are
 * programmer errors and are trapped by assert() in debug builds.
 */
Stone board_get(const Board *board, int row, int col);
void board_set(Board *board, int row, int col, Stone stone);

/* Resets the board to the standard Othello starting position. */
void board_init(Board *board);

/* Counts how many cells currently hold the given stone. */
int board_count(const Board *board, Stone stone);

#endif
