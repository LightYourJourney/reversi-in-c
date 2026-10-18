#ifndef REVERSI_TESTS_TEST_ORACLE_H
#define REVERSI_TESTS_TEST_ORACLE_H

#include "core/board.h"

#include <stdbool.h>

/*
 * Independent, deliberately simple legality check used by randomized tests.
 * It is test-only and intentionally does not call into core/rules.
 */
static inline bool oracle_is_legal_move(const Board *board, Stone player,
                                        Move move)
{
    if (board == NULL || !stone_is_player(player)) {
        return false;
    }
    if (!board_in_bounds(move.row, move.col)) {
        return false;
    }
    if (board_get(board, move.row, move.col) != STONE_EMPTY) {
        return false;
    }

    static const int directions[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1},
        { 0, -1},          { 0, 1},
        { 1, -1}, { 1, 0}, { 1, 1}
    };
    const Stone opponent = stone_opponent(player);

    for (int i = 0; i < 8; ++i) {
        int row = move.row + directions[i][0];
        int col = move.col + directions[i][1];
        int saw_opponent = 0;

        while (board_in_bounds(row, col)
               && board_get(board, row, col) == opponent) {
            saw_opponent = 1;
            row += directions[i][0];
            col += directions[i][1];
        }

        if (saw_opponent && board_in_bounds(row, col)
            && board_get(board, row, col) == player) {
            return true;
        }
    }

    return false;
}

#endif
