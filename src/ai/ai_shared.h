#ifndef REVERSI_AI_AI_HASH_H
#define REVERSI_AI_AI_HASH_H

/*
 * Pieces shared by the midgame search and the endgame solver: a monotonic
 * millisecond clock, the Zobrist keys both transposition tables use, and
 * the move list built from a legal-move bitmask. Internal to src/ai/.
 */

#include "core/board.h"

#include <stdint.h>

typedef struct AiZobrist {
    uint64_t cell[REVERSI_BOARD_CELL_COUNT][2]; /* [cell][0 black, 1 white] */
    uint64_t turn[2];                           /* side to move */
} AiZobrist;

/* Monotonic milliseconds. Zero only disables time budgets; depth and node
 * limits still bound a search, so a missing clock degrades instead of
 * breaking it. */
uint64_t ai_now_ms(void);

/* Fixed keys: the same position hashes the same in every search. */
void ai_zobrist_init(AiZobrist *zobrist);

/* Full hash of a position given as black/white bitmasks. */
uint64_t ai_zobrist_position(const AiZobrist *zobrist, uint64_t black,
                             uint64_t white, Stone to_move);

/* `hash` after `player` plays at `index` flipping `flips`; the turn always
 * passes to the opponent (an auto-pass is handled as its own node). */
uint64_t ai_zobrist_after_move(const AiZobrist *zobrist, uint64_t hash,
                               Stone player, int index, uint64_t flips);

/* Moves for the set bits of `bits`, in board order. Returns the count. */
int ai_moves_from_bits(uint64_t bits, Move *moves);

#endif
