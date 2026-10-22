#include "ai/ai_endgame.h"

#include "ai/ai_internal.h"
#include "core/bitboard.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <time.h>
#endif

#define REVERSI_ENDGAME_TT_SIZE (1u << 16)
#define REVERSI_ENDGAME_CHECK_MASK 1023u

typedef enum EndgameFlag {
    ENDGAME_EXACT = 0,
    ENDGAME_LOWER,
    ENDGAME_UPPER
} EndgameFlag;

typedef struct EndgameTtEntry {
    uint64_t key;
    int score;
    int empties;
    EndgameFlag flag;
    Move best;
} EndgameTtEntry;

typedef struct EndgameSearch {
    const AiConfig *config;
    AiStats stats;
    uint64_t start_ms;
    bool stopped;
    bool cancelled;
    bool timed_out;
    uint64_t zobrist[REVERSI_BOARD_CELL_COUNT][2];
    uint64_t turn_key[2];
    EndgameTtEntry *tt;
    uint32_t tt_mask;
} EndgameSearch;

static uint64_t endgame_now_ms(void)
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

static uint64_t endgame_splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void endgame_init_zobrist(EndgameSearch *search)
{
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (int cell = 0; cell < REVERSI_BOARD_CELL_COUNT; ++cell) {
        search->zobrist[cell][0] = endgame_splitmix64(&seed);
        search->zobrist[cell][1] = endgame_splitmix64(&seed);
    }
    search->turn_key[0] = endgame_splitmix64(&seed);
    search->turn_key[1] = endgame_splitmix64(&seed);
}

static uint64_t endgame_hash(const EndgameSearch *search, const Bitboard *bb,
                             Stone player)
{
    uint64_t hash = (player == STONE_BLACK)
        ? search->turn_key[0] : search->turn_key[1];
    uint64_t remaining = bb->black;
    for (int cell = bitboard_next_index(remaining); cell >= 0;
         cell = bitboard_next_index(remaining)) {
        hash ^= search->zobrist[cell][0];
        remaining &= remaining - 1;
    }
    remaining = bb->white;
    for (int cell = bitboard_next_index(remaining); cell >= 0;
         cell = bitboard_next_index(remaining)) {
        hash ^= search->zobrist[cell][1];
        remaining &= remaining - 1;
    }
    return hash;
}

static uint64_t endgame_hash_move(const EndgameSearch *search, uint64_t hash,
                                  Stone player, int index, uint64_t flips)
{
    const int player_index = (player == STONE_BLACK) ? 0 : 1;
    hash ^= search->zobrist[index][player_index];
    uint64_t remaining = flips;
    for (int cell = bitboard_next_index(remaining); cell >= 0;
         cell = bitboard_next_index(remaining)) {
        hash ^= search->zobrist[cell][0] ^ search->zobrist[cell][1];
        remaining &= remaining - 1;
    }
    return hash ^ search->turn_key[0] ^ search->turn_key[1];
}

static bool endgame_should_stop(EndgameSearch *search)
{
    if (search->stopped) {
        return true;
    }
    if ((search->stats.nodes & REVERSI_ENDGAME_CHECK_MASK) != 0) {
        return false;
    }
    if (search->config->should_cancel != NULL
        && search->config->should_cancel(search->config->cancel_context)) {
        search->cancelled = true;
        search->stopped = true;
        return true;
    }
    if (search->config->time_budget_ms > 0
        && search->start_ms != 0
        && endgame_now_ms() - search->start_ms
            >= (uint64_t)search->config->time_budget_ms) {
        search->timed_out = true;
        search->stopped = true;
        return true;
    }
    return false;
}

static bool endgame_force_stop(EndgameSearch *search)
{
    if (search->stopped) {
        return true;
    }
    if (search->config->should_cancel != NULL
        && search->config->should_cancel(search->config->cancel_context)) {
        search->cancelled = true;
        search->stopped = true;
        return true;
    }
    if (search->config->time_budget_ms > 0
        && search->start_ms != 0
        && endgame_now_ms() - search->start_ms
            >= (uint64_t)search->config->time_budget_ms) {
        search->timed_out = true;
        search->stopped = true;
        return true;
    }
    return false;
}

static int endgame_terminal_score(const Bitboard *bb, Stone player)
{
    const int own = bitboard_count(bb, player);
    const int opp = bitboard_count(bb, stone_opponent(player));
    const int diff = own - opp;
    if (diff > 0) {
        return REVERSI_AI_TERMINAL_WIN + diff;
    }
    if (diff < 0) {
        return -REVERSI_AI_TERMINAL_WIN + diff;
    }
    return 0;
}

static int endgame_move_score(Move move)
{
    const int positional =
        ai_positional_value(move.row, move.col) * 4;
    const bool corner = (move.row == 0 || move.row == REVERSI_BOARD_SIZE - 1)
        && (move.col == 0 || move.col == REVERSI_BOARD_SIZE - 1);
    return positional + (corner ? 1000 : 0);
}

static int moves_from_bits(uint64_t bits, Move *moves)
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

static void order_moves(Move *moves, int count)
{
    for (int i = 1; i < count; ++i) {
        const Move value = moves[i];
        const int score = endgame_move_score(value);
        int j = i;
        while (j > 0 && endgame_move_score(moves[j - 1]) < score) {
            moves[j] = moves[j - 1];
            --j;
        }
        moves[j] = value;
    }
}

static int solve_node(EndgameSearch *search, const Bitboard *bb, uint64_t hash,
                      Stone player, int alpha, int beta)
{
    ++search->stats.nodes;
    if (endgame_should_stop(search)) {
        return 0;
    }

    const uint64_t legal = bitboard_legal_moves(bb, player);
    if (legal == 0) {
        if (bitboard_legal_moves(bb, stone_opponent(player)) == 0) {
            return endgame_terminal_score(bb, player);
        }
        return -solve_node(search, bb,
                           hash ^ search->turn_key[0] ^ search->turn_key[1],
                           stone_opponent(player), -beta, -alpha);
    }

    const int empties = bitboard_popcount(bitboard_empty(bb));
    Move tt_best = {0, 0};
    bool has_tt_best = false;

    if (search->tt != NULL) {
        EndgameTtEntry *entry = &search->tt[hash & search->tt_mask];
        if (entry->empties > 0 && entry->key == hash) {
            tt_best = entry->best;
            has_tt_best = true;
            if (entry->empties >= empties) {
                ++search->stats.tt_hits;
                if (entry->flag == ENDGAME_EXACT) {
                    return entry->score;
                }
                if (entry->flag == ENDGAME_LOWER && entry->score > alpha) {
                    alpha = entry->score;
                } else if (entry->flag == ENDGAME_UPPER
                           && entry->score < beta) {
                    beta = entry->score;
                }
                if (alpha >= beta) {
                    return entry->score;
                }
            }
        }
    }

    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = moves_from_bits(legal, moves);
    order_moves(moves, count);
    if (has_tt_best) {
        for (int i = 0; i < count; ++i) {
            if (moves[i].row == tt_best.row && moves[i].col == tt_best.col) {
                const Move first = moves[0];
                moves[0] = moves[i];
                moves[i] = first;
                break;
            }
        }
    }

    const int alpha_window = alpha;
    const int beta_window = beta;
    int best = -REVERSI_AI_INF;
    Move best_move = moves[0];

    for (int i = 0; i < count; ++i) {
        Bitboard child = *bb;
        uint64_t flips = 0;
        const int index = (int)bitboard_index(moves[i].row, moves[i].col);
        if (!bitboard_apply_move(&child, player, index, &flips)) {
            continue;
        }
        const uint64_t child_hash =
            endgame_hash_move(search, hash, player, index, flips);
        const int score = -solve_node(search, &child, child_hash,
                                      stone_opponent(player), -beta, -alpha);
        if (search->stopped) {
            return 0;
        }
        if (score > best) {
            best = score;
            best_move = moves[i];
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            ++search->stats.cutoffs;
            break;
        }
    }

    if (search->tt != NULL) {
        EndgameTtEntry *entry = &search->tt[hash & search->tt_mask];
        entry->key = hash;
        entry->score = best;
        entry->empties = empties;
        entry->best = best_move;
        if (best <= alpha_window) {
            entry->flag = ENDGAME_UPPER;
        } else if (best >= beta_window) {
            entry->flag = ENDGAME_LOWER;
        } else {
            entry->flag = ENDGAME_EXACT;
        }
    }

    return best;
}

bool ai_endgame_solve(const GameState *state, const AiConfig *config,
                      int max_empties, Move *out_move, int *out_score,
                      AiStats *out_stats, AiEndgameStatus *out_status)
{
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
    }
    if (out_status != NULL) {
        *out_status = AI_ENDGAME_INCOMPLETE;
    }
    if (state == NULL || config == NULL || out_move == NULL
        || max_empties < 0 || !ai_config_is_valid(config)) {
        return false;
    }

    Bitboard bb;
    bitboard_from_board(&state->board, &bb);
    const int empties = bitboard_popcount(bitboard_empty(&bb));
    if (empties == 0 || empties > max_empties) {
        return false;
    }
    const uint64_t legal = bitboard_legal_moves(&bb, state->current_player);
    if (legal == 0) {
        return false;
    }

    EndgameSearch search;
    memset(&search, 0, sizeof(search));
    search.config = config;
    search.start_ms = endgame_now_ms();
    endgame_init_zobrist(&search);
    if (empties >= 8) {
        search.tt = calloc(REVERSI_ENDGAME_TT_SIZE, sizeof(EndgameTtEntry));
        search.tt_mask = REVERSI_ENDGAME_TT_SIZE - 1u;
    }

    const uint64_t hash = endgame_hash(&search, &bb, state->current_player);
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = moves_from_bits(legal, moves);
    order_moves(moves, count);

    int alpha = -REVERSI_AI_INF;
    int best = -REVERSI_AI_INF;
    Move best_move = moves[0];

    for (int i = 0; i < count; ++i) {
        if (endgame_force_stop(&search)) {
            break;
        }
        Bitboard child = bb;
        uint64_t flips = 0;
        const int index = (int)bitboard_index(moves[i].row, moves[i].col);
        if (!bitboard_apply_move(&child, state->current_player, index,
                                 &flips)) {
            continue;
        }
        const uint64_t child_hash = endgame_hash_move(
            &search, hash, state->current_player, index, flips);
        const int score = -solve_node(&search, &child, child_hash,
                                      stone_opponent(state->current_player),
                                      -REVERSI_AI_INF, -alpha);
        if (search.stopped) {
            break;
        }
        if (score > best) {
            best = score;
            best_move = moves[i];
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    const bool complete = !search.stopped;
    free(search.tt);

    if (best == -REVERSI_AI_INF) {
        if (complete) {
            return false;
        }
        /* Cancelled before any root subtree finished: still hand back the
           first ordered legal move, flagged INCOMPLETE by the caller. */
        best_move = moves[0];
        best = 0;
    }
    if (out_stats != NULL) {
        *out_stats = search.stats;
        out_stats->depth = empties;
        out_stats->score = best;
        out_stats->elapsed_ms =
            (int)(endgame_now_ms() - search.start_ms);
        out_stats->cancelled = search.cancelled;
        out_stats->timed_out = search.timed_out;
    }
    *out_move = best_move;
    if (out_score != NULL) {
        *out_score = best;
    }
    if (out_status != NULL) {
        *out_status = complete ? AI_ENDGAME_EXACT : AI_ENDGAME_INCOMPLETE;
    }
    return true;
}
