#ifndef REVERSI_CORE_BOARD_H
#define REVERSI_CORE_BOARD_H

#include <stdbool.h>

#define REVERSI_BOARD_SIZE 8
#define REVERSI_BOARD_CELL_COUNT (REVERSI_BOARD_SIZE * REVERSI_BOARD_SIZE)

/*
 * Coordinates are zero-based: row 0 is the top edge, column 0 is the left
 * edge. Notation uses standard chess files/ranks: "a1" is the bottom-left
 * cell and "h8" is the top-right cell.
 *
 *   col = file - 'a'
 *   row = '8' - rank
 */
typedef enum Stone {
    STONE_EMPTY = 0,
    STONE_BLACK = 1,
    STONE_WHITE = 2
} Stone;

typedef struct Move {
    int row;
    int col;
} Move;

typedef struct Board {
    Stone cells[REVERSI_BOARD_SIZE][REVERSI_BOARD_SIZE];
} Board;

/* Returns true when (row, col) addresses a cell on the board. */
bool board_in_bounds(int row, int col);

/* Returns true when every cell is occupied. NULL returns false. */
bool board_is_full(const Board *board);

/* Returns true when every cell holds EMPTY, BLACK, or WHITE.
 * NULL returns false. */
bool board_is_valid(const Board *board);

/*
 * Safe cell access. board_get returns STONE_EMPTY for a NULL board or
 * out-of-bounds coordinates. board_set returns false and leaves the board
 * unchanged for invalid input.
 */
Stone board_get(const Board *board, int row, int col);
bool board_set(Board *board, int row, int col, Stone stone);

/* Clears every cell. NULL is ignored. */
void board_clear(Board *board);

/*
 * Resets to the standard Othello opening:
 *   d4 = White, e5 = White, d5 = Black, e4 = Black.
 * Black moves first.
 */
void board_init(Board *board);

/* Counts cells holding the given stone. NULL returns 0. */
int board_count(const Board *board, Stone stone);

/* Returns the opposing stone; EMPTY stays EMPTY. */
Stone stone_opponent(Stone stone);

/* True when stone is BLACK or WHITE (a side that can move). */
bool stone_is_player(Stone stone);

/*
 * Converts between a Move and standard notation ("d4"). File letters are
 * case-insensitive on input. The two-character input must be exactly two
 * characters long. On failure the output is unchanged.
 */
bool move_from_notation(const char *text, Move *out_move);
bool move_to_notation(Move move, char out[3]);

#endif
