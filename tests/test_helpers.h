#ifndef REVERSI_TESTS_TEST_HELPERS_H
#define REVERSI_TESTS_TEST_HELPERS_H

#include "core/board.h"
#include "core/game.h"

#include <stdbool.h>

static inline bool boards_equal(const Board *a, const Board *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (a->cells[row][col] != b->cells[row][col]) {
                return false;
            }
        }
    }
    return true;
}

static inline bool moves_equal(Move a, Move b)
{
    return a.row == b.row && a.col == b.col;
}

static inline bool move_in_list(const Move *moves, int count, Move move)
{
    if (moves == NULL || count <= 0) {
        return false;
    }

    for (int i = 0; i < count; ++i) {
        if (moves_equal(moves[i], move)) {
            return true;
        }
    }
    return false;
}

static inline bool game_states_equal(const GameState *a, const GameState *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }

    return boards_equal(&a->board, &b->board)
        && a->current_player == b->current_player
        && a->result == b->result;
}

#endif
