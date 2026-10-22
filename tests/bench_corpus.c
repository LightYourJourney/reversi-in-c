#include "ai/ai.h"
#include "ai/ai_search.h"
#include "core/board.h"
#include "core/game.h"
#include "core/rules.h"

#if defined(__has_include)
#if __has_include("core/bitboard.h")
#define HAVE_BITBOARD 1
#endif
#endif
#ifdef HAVE_BITBOARD
#include "core/bitboard.h"
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <time.h>
#endif

#define CORPUS_SIZE 14
#define MICRO_ITERATIONS 10000

static volatile uint64_t g_sink;

static long peak_rss_kb(void)
{
#if defined(_WIN32)
    return -1;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
#if defined(__APPLE__)
    return usage.ru_maxrss / 1024; /* macOS reports bytes */
#else
    return usage.ru_maxrss;        /* Linux reports KiB */
#endif
#endif
}

typedef struct CorpusEntry {
    const char *name;
    GameState state;
    int empty;
} CorpusEntry;

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static uint64_t now_ns(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64() * 1000000u;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

static int empty_count(const Board *board)
{
    return REVERSI_BOARD_CELL_COUNT - board_count(board, STONE_BLACK)
        - board_count(board, STONE_WHITE);
}

static int legal_count(const GameState *state, Stone player)
{
    return rules_count_legal_moves(&state->board, player);
}

static bool random_ply(GameState *state, uint64_t *rng)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &state->board, state->current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    if (count <= 0) {
        return false;
    }
    return game_apply_move(state, moves[next_random(rng) % (uint64_t)count]);
}

static bool play_to_empty(uint64_t seed, int target, GameState *out)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed;
    while (empty_count(&state.board) > target) {
        if (!random_ply(&state, &rng)) {
            return false;
        }
    }
    *out = state;
    return empty_count(&state.board) == target;
}

static bool play_to_end(uint64_t seed, GameState *out)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed;
    for (int ply = 0; ply < 200; ++ply) {
        if (state.result != GAME_RESULT_ONGOING) {
            break;
        }
        if (!random_ply(&state, &rng)) {
            break;
        }
    }
    *out = state;
    return true;
}

/* Finds a position matching a predicate while walking random games. */
typedef bool (*PositionPredicate)(const GameState *state);

static bool find_position(uint64_t seed, PositionPredicate predicate,
                          GameState *out)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed;
    for (int ply = 0; ply < 200; ++ply) {
        if (predicate(&state)) {
            *out = state;
            return true;
        }
        if (!random_ply(&state, &rng)) {
            break;
        }
    }
    return false;
}

static bool is_corner_contest(const GameState *state)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &state->board, state->current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    for (int i = 0; i < count; ++i) {
        if ((moves[i].row == 0 || moves[i].row == REVERSI_BOARD_SIZE - 1)
            && (moves[i].col == 0
                || moves[i].col == REVERSI_BOARD_SIZE - 1)) {
            return true;
        }
    }
    return false;
}

static bool is_mobility_limited(const GameState *state)
{
    return legal_count(state, state->current_player) > 0
        && legal_count(state, state->current_player) <= 3;
}

static bool is_pass_candidate(const GameState *state)
{
    return legal_count(state, state->current_player) > 0
        && legal_count(state, stone_opponent(state->current_player)) == 0;
}

static double micro_movegen_scalar(const GameState *state)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const uint64_t start = now_ns();
    int checksum = 0;
    for (int i = 0; i < MICRO_ITERATIONS; ++i) {
        checksum += rules_generate_legal_moves(
            &state->board, state->current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
    }
    g_sink += (uint64_t)checksum;
    return (double)(now_ns() - start) / MICRO_ITERATIONS;
}

static double micro_apply_scalar(const GameState *state)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &state->board, state->current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    if (count <= 0) {
        return 0.0;
    }
    const Move move = moves[0];
    const uint64_t start = now_ns();
    int checksum = 0;
    for (int i = 0; i < MICRO_ITERATIONS; ++i) {
        Board board = state->board;
        checksum += rules_apply_move(&board, state->current_player, move);
    }
    g_sink += (uint64_t)checksum;
    return (double)(now_ns() - start) / MICRO_ITERATIONS;
}

#ifdef HAVE_BITBOARD
static double micro_movegen_bitboard(const Bitboard *bb, Stone player)
{
    const uint64_t start = now_ns();
    uint64_t checksum = 0;
    for (int i = 0; i < MICRO_ITERATIONS; ++i) {
        checksum ^= bitboard_legal_moves(bb, player);
    }
    g_sink += checksum;
    return (double)(now_ns() - start) / MICRO_ITERATIONS;
}

static double micro_apply_bitboard(const Bitboard *bb, Stone player)
{
    const uint64_t legal = bitboard_legal_moves(bb, player);
    if (legal == 0) {
        return 0.0;
    }
    const int index = bitboard_next_index(legal);
    const uint64_t start = now_ns();
    uint64_t checksum = 0;
    for (int i = 0; i < MICRO_ITERATIONS; ++i) {
        Bitboard copy = *bb;
        checksum += bitboard_apply_move(&copy, player, index, NULL) ? 1 : 0;
    }
    g_sink += checksum;
    return (double)(now_ns() - start) / MICRO_ITERATIONS;
}
#endif

static void measure_search(const CorpusEntry *entry)
{
    if (entry->empty == 0
        || legal_count(&entry->state, entry->state.current_player) == 0) {
        printf("%-18s search=skip\n", entry->name);
        return;
    }

    AiConfig fixed = ai_default_config(AI_DIFFICULTY_MEDIUM, 1);
    fixed.max_depth = 6;
    fixed.time_budget_ms = 10000;
    Move move = {0, 0};
    AiStats fixed_stats = {0};
    const uint64_t fixed_start = now_ns();
    const bool fixed_ok = ai_search_choose_ex(
        &entry->state, &fixed, false, &move, &fixed_stats);
    const uint64_t fixed_ns = now_ns() - fixed_start;

    AiConfig practical = ai_default_config(AI_DIFFICULTY_HARD, 1);
    AiStats practical_stats = {0};
    const uint64_t practical_start = now_ns();
    const bool practical_ok = ai_search_choose(
        &entry->state, &practical, &move, &practical_stats);
    const uint64_t practical_ns = now_ns() - practical_start;

    const double hard_cut = practical_stats.nodes > 0
        ? (double)practical_stats.cutoffs * 100.0
            / (double)practical_stats.nodes
        : 0.0;
    printf("%-18s fixed6=%s nodes=%-9" PRIu64 " ms=%.3f nps=%.0f"
           " | hard=%s depth=%d nodes=%-9" PRIu64 " ms=%.1f"
           " cut=%.1f%% tt=%" PRIu64 "\n",
           entry->name, fixed_ok ? "ok" : "no", fixed_stats.nodes,
           (double)fixed_ns / 1e6,
           fixed_ns > 0 ? (double)fixed_stats.nodes * 1e9 / (double)fixed_ns
                        : 0.0,
           practical_ok ? "ok" : "no", practical_stats.depth,
           practical_stats.nodes, (double)practical_ns / 1e6,
           hard_cut, practical_stats.tt_hits);
}

int main(void)
{
    CorpusEntry corpus[CORPUS_SIZE];
    memset(corpus, 0, sizeof(corpus));

    corpus[0].name = "opening";
    game_init(&corpus[0].state);

    corpus[1].name = "midgame-40";
    if (!play_to_empty(0x1111ULL, 40, &corpus[1].state)) {
        memset(&corpus[1].state, 0, sizeof(corpus[1].state));
    }

    corpus[2].name = "midgame-30";
    if (!play_to_empty(0x1212ULL, 30, &corpus[2].state)) {
        memset(&corpus[2].state, 0, sizeof(corpus[2].state));
    }

    corpus[3].name = "corner-contest";
    if (!find_position(0x2222ULL, is_corner_contest, &corpus[3].state)) {
        memset(&corpus[3].state, 0, sizeof(corpus[3].state));
    }

    corpus[4].name = "mobility-<=3";
    if (!find_position(0x3333ULL, is_mobility_limited, &corpus[4].state)) {
        memset(&corpus[4].state, 0, sizeof(corpus[4].state));
    }

    corpus[5].name = "must-pass";
    if (find_position(0x4444ULL, is_pass_candidate, &corpus[5].state)) {
        /* Hand the turn to the side with no legal move. */
        corpus[5].state.current_player = stone_opponent(
            corpus[5].state.current_player);
    } else {
        memset(&corpus[5].state, 0, sizeof(corpus[5].state));
    }

    corpus[6].name = "terminal";
    if (!play_to_end(0x5555ULL, &corpus[6].state)) {
        memset(&corpus[6].state, 0, sizeof(corpus[6].state));
    }

    static const struct {
        const char *name;
        uint64_t seed;
        int empties;
    } endgames[] = {
        {"endgame-20", 0x6060ULL, 20},
        {"endgame-12", 0x6666ULL, 12},
        {"endgame-10", 0x6A6AULL, 10},
        {"endgame-8", 0x7777ULL, 8},
        {"endgame-6", 0x7878ULL, 6},
        {"endgame-4", 0x8888ULL, 4},
        {"endgame-2", 0x8989ULL, 2}
    };
    for (size_t i = 0; i < sizeof(endgames) / sizeof(endgames[0]); ++i) {
        CorpusEntry *entry = &corpus[7 + i];
        entry->name = endgames[i].name;
        if (!play_to_empty(endgames[i].seed, endgames[i].empties,
                           &entry->state)) {
            memset(&entry->state, 0, sizeof(entry->state));
        }
    }

    printf("Reversi AI corpus benchmark\n");
    printf("micro iterations per position: %d\n", MICRO_ITERATIONS);
    printf("fixed search: Medium depth 6, no TT; practical: Hard default "
           "(500 ms budget)\n\n");
    printf("%-18s %5s %10s %10s %10s %10s\n",
           "position", "empty", "scalar-gen", "scalar-play",
           "bb-gen", "bb-play");
    for (int i = 0; i < CORPUS_SIZE; ++i) {
        CorpusEntry *entry = &corpus[i];
        entry->empty = empty_count(&entry->state.board);
        const bool playable =
            legal_count(&entry->state, entry->state.current_player) > 0;
        double bb_gen = -1.0;
        double bb_play = -1.0;
#ifdef HAVE_BITBOARD
        Bitboard bb;
        bitboard_from_board(&entry->state.board, &bb);
        bb_gen = micro_movegen_bitboard(&bb, entry->state.current_player);
        if (playable) {
            bb_play = micro_apply_bitboard(&bb, entry->state.current_player);
        }
#endif
        printf("%-18s %5d %10.1f %10.1f %10.1f %10.1f\n",
               entry->name, entry->empty,
               micro_movegen_scalar(&entry->state),
               playable ? micro_apply_scalar(&entry->state) : 0.0,
               bb_gen, bb_play);
    }

    printf("\nsearch (ns/op above; times below)\n");
    for (int i = 0; i < CORPUS_SIZE; ++i) {
        measure_search(&corpus[i]);
    }
    printf("\npeak_rss_kb=%ld\n", peak_rss_kb());
    return 0;
}
