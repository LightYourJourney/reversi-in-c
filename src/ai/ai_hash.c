#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ai/ai_hash.h"

#include "core/bitboard.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <time.h>
#endif

uint64_t ai_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#elif defined(__EMSCRIPTEN__)
    return (uint64_t)emscripten_get_now();
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
#else
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void ai_zobrist_init(AiZobrist *zobrist)
{
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (int cell = 0; cell < REVERSI_BOARD_CELL_COUNT; ++cell) {
        zobrist->cell[cell][0] = splitmix64(&seed);
        zobrist->cell[cell][1] = splitmix64(&seed);
    }
    zobrist->turn[0] = splitmix64(&seed);
    zobrist->turn[1] = splitmix64(&seed);
}

uint64_t ai_zobrist_position(const AiZobrist *zobrist, uint64_t black,
                             uint64_t white, Stone to_move)
{
    uint64_t hash = zobrist->turn[to_move == STONE_BLACK ? 0 : 1];
    for (int cell = bitboard_next_index(black); cell >= 0;
         cell = bitboard_next_index(black)) {
        hash ^= zobrist->cell[cell][0];
        black &= black - 1;
    }
    for (int cell = bitboard_next_index(white); cell >= 0;
         cell = bitboard_next_index(white)) {
        hash ^= zobrist->cell[cell][1];
        white &= white - 1;
    }
    return hash;
}

uint64_t ai_zobrist_after_move(const AiZobrist *zobrist, uint64_t hash,
                               Stone player, int index, uint64_t flips)
{
    hash ^= zobrist->cell[index][player == STONE_BLACK ? 0 : 1];
    for (int cell = bitboard_next_index(flips); cell >= 0;
         cell = bitboard_next_index(flips)) {
        hash ^= zobrist->cell[cell][0] ^ zobrist->cell[cell][1];
        flips &= flips - 1;
    }
    return hash ^ zobrist->turn[0] ^ zobrist->turn[1];
}

int ai_moves_from_bits(uint64_t bits, Move *moves)
{
    int count = 0;
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        if ((bits & (1ULL << index)) != 0) {
            moves[count].row = index / REVERSI_BOARD_SIZE;
            moves[count].col = index % REVERSI_BOARD_SIZE;
            ++count;
        }
    }
    return count;
}
