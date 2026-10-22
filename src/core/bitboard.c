#include "core/bitboard.h"

#include <stddef.h>

#if defined(__has_builtin)
#if __has_builtin(__builtin_popcountll)
#define REVERSI_BITBOARD_USE_BUILTIN 1
#endif
#if __has_builtin(__builtin_ctzll)
#define REVERSI_BITBOARD_USE_CTZ 1
#endif
#endif

static uint64_t shift_east(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_H) << 1;
}

static uint64_t shift_west(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_A) >> 1;
}

static uint64_t shift_south(uint64_t x)
{
    return x << 8;
}

static uint64_t shift_north(uint64_t x)
{
    return x >> 8;
}

static uint64_t shift_south_east(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_H) << 9;
}

static uint64_t shift_south_west(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_A) << 7;
}

static uint64_t shift_north_east(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_H) >> 7;
}

static uint64_t shift_north_west(uint64_t x)
{
    return (x & ~REVERSI_BITBOARD_FILE_A) >> 9;
}

/* Walks one direction from own stones and records empty destinations that
   would bracket at least one opponent stone. */
#define REVERSI_BB_LEGAL_DIR(shift, own, opp, empty, moves)     \
    do {                                                        \
        uint64_t walk = (shift)(own) & (opp);                   \
        while (walk != 0) {                                     \
            const uint64_t next = (shift)(walk);                \
            (moves) |= next & (empty);                          \
            walk = next & (opp);                                \
        }                                                       \
    } while (0)

/* Walks one direction from the placed stone and collects the bracketed run
   when it terminates on an own stone. */
#define REVERSI_BB_FLIP_DIR(shift, own, opp, move_bit, flips)   \
    do {                                                        \
        uint64_t walk = (shift)(move_bit) & (opp);              \
        uint64_t captured = 0;                                  \
        while (walk != 0) {                                     \
            captured |= walk;                                   \
            const uint64_t next = (shift)(walk);                \
            if ((next & (own)) != 0) {                          \
                (flips) |= captured;                            \
                break;                                          \
            }                                                   \
            walk = next & (opp);                                \
        }                                                       \
    } while (0)

static uint64_t own_bits(const Bitboard *bb, Stone player)
{
    if (player == STONE_BLACK) {
        return bb->black;
    }
    if (player == STONE_WHITE) {
        return bb->white;
    }
    return 0;
}

static uint64_t opponent_bits(const Bitboard *bb, Stone player)
{
    if (player == STONE_BLACK) {
        return bb->white;
    }
    if (player == STONE_WHITE) {
        return bb->black;
    }
    return 0;
}

unsigned bitboard_index(int row, int col)
{
    return (unsigned)(row * REVERSI_BOARD_SIZE + col);
}

bool bitboard_valid(const Bitboard *bb)
{
    return bb != NULL && (bb->black & bb->white) == 0;
}

void bitboard_from_board(const Board *board, Bitboard *out)
{
    if (out == NULL) {
        return;
    }
    out->black = 0;
    out->white = 0;
    if (board == NULL) {
        return;
    }
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board->cells[row][col];
            const unsigned index = bitboard_index(row, col);
            if (stone == STONE_BLACK) {
                out->black |= 1ULL << index;
            } else if (stone == STONE_WHITE) {
                out->white |= 1ULL << index;
            }
        }
    }
}

bool bitboard_to_board(const Bitboard *bb, Board *out)
{
    if (out == NULL || !bitboard_valid(bb)) {
        return false;
    }
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const unsigned index = bitboard_index(row, col);
            const uint64_t bit = 1ULL << index;
            if ((bb->black & bit) != 0) {
                out->cells[row][col] = STONE_BLACK;
            } else if ((bb->white & bit) != 0) {
                out->cells[row][col] = STONE_WHITE;
            } else {
                out->cells[row][col] = STONE_EMPTY;
            }
        }
    }
    return true;
}

int bitboard_popcount(uint64_t bits)
{
#if defined(REVERSI_BITBOARD_USE_BUILTIN)
    return __builtin_popcountll(bits);
#else
    bits = bits - ((bits >> 1) & 0x5555555555555555ULL);
    bits = (bits & 0x3333333333333333ULL)
        + ((bits >> 2) & 0x3333333333333333ULL);
    bits = (bits + (bits >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((bits * 0x0101010101010101ULL) >> 56);
#endif
}

int bitboard_next_index(uint64_t bits)
{
    if (bits == 0) {
        return -1;
    }
#if defined(REVERSI_BITBOARD_USE_CTZ)
    return __builtin_ctzll(bits);
#else
    int index = 0;
    while ((bits & 1ULL) == 0) {
        bits >>= 1;
        ++index;
    }
    return index;
#endif
}

uint64_t bitboard_occupied(const Bitboard *bb)
{
    if (bb == NULL) {
        return 0;
    }
    return bb->black | bb->white;
}

uint64_t bitboard_empty(const Bitboard *bb)
{
    return ~bitboard_occupied(bb);
}

uint64_t bitboard_player(const Bitboard *bb, Stone player)
{
    return bb != NULL ? own_bits(bb, player) : 0;
}

int bitboard_count(const Bitboard *bb, Stone player)
{
    return bitboard_popcount(own_bits(bb, player));
}

int bitboard_disc_diff(const Bitboard *bb, Stone player)
{
    const Stone other =
        (player == STONE_BLACK) ? STONE_WHITE : STONE_BLACK;
    return bitboard_count(bb, player) - bitboard_count(bb, other);
}

uint64_t bitboard_legal_moves(const Bitboard *bb, Stone player)
{
    if (!bitboard_valid(bb)) {
        return 0;
    }
    const uint64_t own = own_bits(bb, player);
    const uint64_t opp = opponent_bits(bb, player);
    const uint64_t empty = bitboard_empty(bb);
    uint64_t moves = 0;

    REVERSI_BB_LEGAL_DIR(shift_east, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_west, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_south, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_north, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_south_east, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_south_west, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_north_east, own, opp, empty, moves);
    REVERSI_BB_LEGAL_DIR(shift_north_west, own, opp, empty, moves);
    return moves;
}

uint64_t bitboard_flips(const Bitboard *bb, Stone player, int index)
{
    if (!bitboard_valid(bb) || index < 0 || index >= REVERSI_BOARD_CELL_COUNT) {
        return 0;
    }
    const uint64_t move_bit = 1ULL << (unsigned)index;
    const uint64_t occupied = bitboard_occupied(bb);
    if ((occupied & move_bit) != 0) {
        return 0;
    }
    const uint64_t own = own_bits(bb, player);
    const uint64_t opp = opponent_bits(bb, player);
    uint64_t flips = 0;

    REVERSI_BB_FLIP_DIR(shift_east, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_west, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_south, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_north, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_south_east, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_south_west, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_north_east, own, opp, move_bit, flips);
    REVERSI_BB_FLIP_DIR(shift_north_west, own, opp, move_bit, flips);
    return flips;
}

bool bitboard_apply_move(Bitboard *bb, Stone player, int index,
                         uint64_t *out_flips)
{
    const uint64_t flips = bitboard_flips(bb, player, index);
    if (flips == 0) {
        return false;
    }
    const uint64_t move_bit = 1ULL << (unsigned)index;
    if (player == STONE_BLACK) {
        bb->black |= move_bit | flips;
        bb->white &= ~flips;
    } else {
        bb->white |= move_bit | flips;
        bb->black &= ~flips;
    }
    if (out_flips != NULL) {
        *out_flips = flips;
    }
    return true;
}

bool bitboard_must_pass(const Bitboard *bb, Stone player)
{
    return bitboard_legal_moves(bb, player) == 0;
}

bool bitboard_is_terminal(const Bitboard *bb)
{
    return bitboard_legal_moves(bb, STONE_BLACK) == 0
        && bitboard_legal_moves(bb, STONE_WHITE) == 0;
}
