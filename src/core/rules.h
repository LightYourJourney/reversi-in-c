#ifndef REVERSI_CORE_RULES_H
#define REVERSI_CORE_RULES_H

#include "core/board.h"

#include <stdbool.h>

/*
 * Stateless Reversi rules. All functions are pure queries except
 * rules_apply_move(), which mutates only the supplied board.
 */

/* True when player may place at move and flip at least one opponent stone. */
bool rules_is_legal_move(const Board *board, Stone player, Move move);

/*
 * Writes up to capacity legal moves in row-major order and returns the
 * number written. Use rules_count_legal_moves() for the total. Invalid
 * arguments (including a NULL moves array or non-positive capacity) return 0.
 */
int rules_generate_legal_moves(const Board *board, Stone player,
                               Move *moves, int capacity);

/* Total number of legal moves; invalid arguments return 0. */
int rules_count_legal_moves(const Board *board, Stone player);

/* True when player has no legal move. Invalid arguments return false. */
bool rules_must_pass(const Board *board, Stone player);

/*
 * Applies the move in place, placing the stone and flipping every bracketed
 * opponent stone. Returns the number of flipped stones (at least 1), or -1
 * for an illegal move. On failure the board is unchanged.
 */
int rules_apply_move(Board *board, Stone player, Move move);

#endif
