#ifndef REVERSI_CORE_BITBOARD_H
#define REVERSI_CORE_BITBOARD_H

#include "core/board.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Bit mapping matches the Board's row-major layout with row 0 at the top:
 *   bit_index = row * 8 + col
 * so bit 0 is a8 (top-left) and bit 63 is h1 (bottom-right).
 */
typedef struct Bitboard {
    uint64_t black;
    uint64_t white;
} Bitboard;

#define REVERSI_BITBOARD_FILE_A 0x0101010101010101ULL
#define REVERSI_BITBOARD_FILE_H 0x8080808080808080ULL

/* Row-major index for in-bounds coordinates. */
unsigned bitboard_index(int row, int col);

/* True when black and white do not share a bit. */
bool bitboard_valid(const Bitboard *bb);

/* Converts between the public Board and the bitboard representation. */
void bitboard_from_board(const Board *board, Bitboard *out);
bool bitboard_to_board(const Bitboard *bb, Board *out);

/* Portable popcount; uses the compiler builtin when one is detected. */
int bitboard_popcount(uint64_t bits);

uint64_t bitboard_occupied(const Bitboard *bb);
uint64_t bitboard_empty(const Bitboard *bb);
uint64_t bitboard_player(const Bitboard *bb, Stone player);

int bitboard_count(const Bitboard *bb, Stone player);
int bitboard_disc_diff(const Bitboard *bb, Stone player);

/* Bit set of legal destinations for player. */
uint64_t bitboard_legal_moves(const Bitboard *bb, Stone player);

/* Flips produced by placing player at index; 0 when the move is illegal. */
uint64_t bitboard_flips(const Bitboard *bb, Stone player, int index);

/*
 * Applies a move. Returns false and leaves the position unchanged when the
 * index is occupied/out of range or the move flips nothing. out_flips is
 * optional and receives the flipped bits.
 */
bool bitboard_apply_move(Bitboard *bb, Stone player, int index,
                         uint64_t *out_flips);

/* Matches rules_must_pass(): the player has no legal destination. */
bool bitboard_must_pass(const Bitboard *bb, Stone player);

/* Neither side has a legal destination. */
bool bitboard_is_terminal(const Bitboard *bb);

#endif
