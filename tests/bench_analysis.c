#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ai/ai_analyze.h"
#include "core/bitboard.h"
#include "core/game.h"
#include "core/rules.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int empty_count(const GameState *state)
{
    Bitboard bb;
    bitboard_from_board(&state->board, &bb);
    return bitboard_popcount(bitboard_empty(&bb));
}

static bool position_with_empties(uint64_t seed, int target, GameState *out)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed;
    while (empty_count(&state) > target) {
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &state.board, state.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            return false;
        }
        const Move move = moves[next_random(&rng) % (uint64_t)count];
        if (!game_apply_move(&state, move)) {
            return false;
        }
    }
    *out = state;
    return empty_count(&state) == target;
}

static void run_case(const char *name, const GameState *state, int depth,
                     int lines)
{
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0x5151u);
    config.max_depth = depth;
    config.time_budget_ms = 10000;

    const uint64_t start = now_ms();
    AiAnalysis analysis;
    const bool ok = ai_analyze_position(state, &config, lines, &analysis);
    const uint64_t elapsed = now_ms() - start;

    if (!ok) {
        printf("%-16s depth=%-2d lines=%d  failed  %" PRIu64 " ms\n",
               name, depth, lines, elapsed);
        return;
    }
    printf("%-16s depth=%-2d lines=%d  ms=%-6" PRIu64
           " nodes=%-9" PRIu64 " reported=%d%s%s\n",
           name, depth, lines, elapsed, analysis.stats.nodes,
           analysis.depth, analysis.complete ? "" : " partial",
           analysis.exact ? " exact" : "");
    for (int i = 0; i < analysis.line_count; ++i) {
        const AiAnalysisLine *line = &analysis.lines[i];
        char notation[3];
        move_to_notation(line->move, notation);
        printf("  %d. %s score=%+d pv=%d\n", i + 1, notation,
               line->score, line->pv_length);
    }
}

int main(void)
{
    printf("Reversi analysis benchmark\n");
    printf("lines=3, Hard evaluation, 10 s cap\n");

    GameState opening;
    game_init(&opening);
    run_case("opening", &opening, 4, 3);
    run_case("opening", &opening, 6, 3);
    run_case("opening", &opening, 8, 3);

    GameState midgame;
    if (position_with_empties(0x2468ULL, 30, &midgame)) {
        run_case("midgame-30", &midgame, 4, 3);
        run_case("midgame-30", &midgame, 6, 3);
        run_case("midgame-30", &midgame, 8, 3);
    }

    GameState endgame;
    if (position_with_empties(0x1357ULL, 8, &endgame)) {
        run_case("endgame-8", &endgame, 8, 3);
    }

    return 0;
}
