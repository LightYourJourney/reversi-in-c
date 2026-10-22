#include "ai/ai_search.h"

#include "ai/ai_internal.h"
#include "core/bitboard.h"
#include "core/rules.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REVERSI_AI_CANCEL_CHECK_MASK 1023u
#define REVERSI_AI_TT_SIZE (1u << 16)

typedef enum AiTtFlag {
    AI_TT_EXACT = 0,
    AI_TT_LOWER,
    AI_TT_UPPER
} AiTtFlag;

typedef struct AiTtEntry {
    uint64_t key;
    int score;
    int depth;
    AiTtFlag flag;
    Move best;
} AiTtEntry;

typedef struct AiSearch {
    const AiConfig *config;
    AiStats stats;
    uint64_t start_ms;
    bool stopped;
    bool cancelled;
    bool timed_out;
    uint64_t zobrist[REVERSI_BOARD_CELL_COUNT][2];
    uint64_t turn_key[2];
    AiTtEntry *tt;
    uint32_t tt_mask;
} AiSearch;

static uint64_t ai_now_ms(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        /* Zero only disables the wall-clock budget; the depth limit still
           bounds the search, so this degrades instead of breaking. */
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static uint64_t rng_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void init_zobrist(AiSearch *search)
{
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (int cell = 0; cell < REVERSI_BOARD_CELL_COUNT; ++cell) {
        search->zobrist[cell][0] = splitmix64(&seed);
        search->zobrist[cell][1] = splitmix64(&seed);
    }
    search->turn_key[0] = splitmix64(&seed);
    search->turn_key[1] = splitmix64(&seed);
}

static uint64_t state_hash(const AiSearch *search, const GameState *state)
{
    uint64_t hash = state->current_player == STONE_BLACK
        ? search->turn_key[0] : search->turn_key[1];
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board_get(&state->board, row, col);
            if (stone == STONE_BLACK || stone == STONE_WHITE) {
                hash ^= search->zobrist[row * REVERSI_BOARD_SIZE + col]
                    [stone == STONE_BLACK ? 0 : 1];
            }
        }
    }
    return hash;
}

static bool check_stop(AiSearch *search, bool force)
{
    if (search->stopped) {
        return true;
    }
    if (!force && (search->stats.nodes & REVERSI_AI_CANCEL_CHECK_MASK) != 0) {
        return false;
    }
    if (search->config->should_cancel != NULL
        && search->config->should_cancel(search->config->cancel_context)) {
        search->cancelled = true;
        search->stopped = true;
        return true;
    }
    if (ai_now_ms() - search->start_ms
        >= (uint64_t)search->config->time_budget_ms) {
        search->timed_out = true;
        search->stopped = true;
        return true;
    }
    return false;
}

static bool should_stop(AiSearch *search)
{
    return check_stop(search, false);
}

static bool should_stop_now(AiSearch *search)
{
    return check_stop(search, true);
}

static bool is_corner_square(int row, int col)
{
    return (row == 0 || row == REVERSI_BOARD_SIZE - 1)
        && (col == 0 || col == REVERSI_BOARD_SIZE - 1);
}

static int move_order_score(Move move)
{
    int score = ai_positional_value(move.row, move.col) * 4;
    if (is_corner_square(move.row, move.col)) {
        score += 1000;
    }
    return score;
}

static bool same_move(Move a, Move b)
{
    return a.row == b.row && a.col == b.col;
}

static void order_moves(Move *moves, int count, Move preferred,
                        bool has_preferred)
{
    if (count <= 1) {
        return;
    }

    /* Insertion sort by static square value; stable for equal scores. */
    for (int i = 1; i < count; ++i) {
        const Move key = moves[i];
        const int key_score = move_order_score(key);
        int j = i - 1;
        while (j >= 0 && move_order_score(moves[j]) < key_score) {
            moves[j + 1] = moves[j];
            --j;
        }
        moves[j + 1] = key;
    }

    /* Promote the TT/previous-iteration move after sorting so it is really
       searched first; sorting beforehand would displace it again. */
    if (has_preferred) {
        for (int i = 0; i < count; ++i) {
            if (same_move(moves[i], preferred)) {
                if (i > 0) {
                    const Move tmp = moves[0];
                    moves[0] = moves[i];
                    moves[i] = tmp;
                }
                break;
            }
        }
    }
}

static uint64_t hash_after_move(const AiSearch *search, uint64_t hash,
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
    /* A move always hands the turn over (auto-pass is handled as a node). */
    return hash ^ search->turn_key[0] ^ search->turn_key[1];
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

static bool apply_search_move(const AiSearch *search, GameState *child,
                              Bitboard *child_bb, uint64_t *child_hash,
                              const GameState *state, const Bitboard *bb,
                              uint64_t hash, Move move)
{
    *child = *state;
    *child_bb = *bb;
    const Stone player = state->current_player;
    if (rules_apply_move(&child->board, player, move) < 0) {
        return false;
    }
    const int index = (int)bitboard_index(move.row, move.col);
    uint64_t flips = 0;
    if (!bitboard_apply_move(child_bb, player, index, &flips)) {
        return false;
    }
    if (child_hash != NULL) {
        *child_hash = hash_after_move(search, hash, player, index, flips);
    }
    child->current_player = stone_opponent(player);
    /* The search detects terminal positions by move generation. */
    child->result = GAME_RESULT_ONGOING;
    return true;
}

static int negamax(AiSearch *search, const GameState *state,
                   const Bitboard *bb, uint64_t hash, int depth,
                   int alpha, int beta)
{
    ++search->stats.nodes;
    if (should_stop(search)) {
        return 0;
    }

    const Stone player = state->current_player;
    const uint64_t legal = bitboard_legal_moves(bb, player);

    if (legal == 0) {
        if (bitboard_legal_moves(bb, stone_opponent(player)) == 0) {
            return ai_terminal_score(state);
        }

        GameState passed = *state;
        passed.current_player = stone_opponent(player);
        const uint64_t passed_hash =
            hash ^ search->turn_key[0] ^ search->turn_key[1];
        return -negamax(search, &passed, bb, passed_hash, depth,
                        -beta, -alpha);
    }

    if (depth <= 0) {
        AiEvaluation evaluation;
        ai_evaluate_for_difficulty(state, search->config->difficulty,
                                   &evaluation);
        return evaluation.total;
    }

    const uint64_t key = hash;
    Move tt_best = {0, 0};
    bool has_tt_best = false;

    if (search->tt != NULL) {
        AiTtEntry *entry = &search->tt[key & search->tt_mask];
        if (entry->depth > 0 && entry->key == key) {
            tt_best = entry->best;
            has_tt_best = true;
            if (entry->depth >= depth) {
                ++search->stats.tt_hits;
                if (entry->flag == AI_TT_EXACT) {
                    return entry->score;
                }
                if (entry->flag == AI_TT_LOWER && entry->score > alpha) {
                    alpha = entry->score;
                } else if (entry->flag == AI_TT_UPPER
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
    order_moves(moves, count, tt_best, has_tt_best);

    /* The TT probe may have tightened alpha/beta. Bound flags must describe
       the window actually searched; using the caller's original window would
       store a fail-low against a raised alpha as EXACT. */
    const int alpha_window = alpha;
    const int beta_window = beta;
    int best = -REVERSI_AI_INF;
    Move best_move = moves[0];
    for (int i = 0; i < count; ++i) {
        GameState child;
        Bitboard child_bb;
        uint64_t child_hash = 0;
        if (!apply_search_move(search, &child, &child_bb, &child_hash,
                               state, bb, hash, moves[i])) {
            continue;
        }
        const int score = -negamax(search, &child, &child_bb, child_hash,
                                   depth - 1, -beta, -alpha);
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
        AiTtEntry *entry = &search->tt[key & search->tt_mask];
        entry->key = key;
        entry->score = best;
        entry->depth = depth;
        entry->best = best_move;
        if (best <= alpha_window) {
            entry->flag = AI_TT_UPPER;
        } else if (best >= beta_window) {
            entry->flag = AI_TT_LOWER;
        } else {
            entry->flag = AI_TT_EXACT;
        }
    }

    return best;
}

static bool root_search(AiSearch *search, const GameState *state,
                        const Bitboard *bb, uint64_t hash, int depth,
                        Move preferred, bool has_preferred,
                        Move *out_move, int *out_score)
{
    const uint64_t legal = bitboard_legal_moves(bb, state->current_player);
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = moves_from_bits(legal, moves);
    if (count == 0) {
        return false;
    }

    order_moves(moves, count, preferred, has_preferred);
    int alpha = -REVERSI_AI_INF;
    int best_score = -REVERSI_AI_INF;
    Move best = moves[0];

    for (int i = 0; i < count; ++i) {
        if (should_stop_now(search)) {
            return false;
        }

        GameState child;
        Bitboard child_bb;
        uint64_t child_hash = 0;
        if (!apply_search_move(search, &child, &child_bb, &child_hash,
                               state, bb, hash, moves[i])) {
            continue;
        }
        const int score = -negamax(search, &child, &child_bb, child_hash,
                                   depth - 1, -REVERSI_AI_INF, -alpha);
        if (search->stopped) {
            return false;
        }
        if (score > best_score) {
            best_score = score;
            best = moves[i];
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    *out_move = best;
    *out_score = best_score;
    return true;
}

static void finish_stats(AiSearch *search, int completed_depth, int score,
                         AiStats *out_stats)
{
    if (out_stats == NULL) {
        return;
    }
    *out_stats = search->stats;
    out_stats->depth = completed_depth;
    out_stats->score = score;
    out_stats->elapsed_ms = (int)(ai_now_ms() - search->start_ms);
    out_stats->cancelled = search->cancelled;
    out_stats->timed_out = search->timed_out;
}

static bool search_choose(const GameState *state, const AiConfig *config,
                          bool use_tt, Move *out_move, AiStats *out_stats)
{
    if (state == NULL || config == NULL || out_move == NULL
        || !ai_config_is_valid(config)) {
        return false;
    }

    AiSearch search;
    memset(&search, 0, sizeof(search));
    search.config = config;
    search.start_ms = ai_now_ms();
    init_zobrist(&search);
    const uint64_t root_hash = state_hash(&search, state);

    if (config->difficulty == AI_DIFFICULTY_HARD && use_tt) {
        search.tt = calloc(REVERSI_AI_TT_SIZE, sizeof(AiTtEntry));
        search.tt_mask = REVERSI_AI_TT_SIZE - 1u;
    }

    Bitboard bb;
    bitboard_from_board(&state->board, &bb);
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = moves_from_bits(
        bitboard_legal_moves(&bb, state->current_player), moves);
    if (count == 0) {
        free(search.tt);
        return false;
    }

    int max_depth = config->max_depth;
    const int black = board_count(&state->board, STONE_BLACK);
    const int white = board_count(&state->board, STONE_WHITE);
    const int empty = REVERSI_BOARD_CELL_COUNT - black - white;
    if (config->difficulty == AI_DIFFICULTY_HARD && empty <= 12
        && max_depth < empty) {
        max_depth = empty;
    }

    Move best = moves[0];
    int best_score = -REVERSI_AI_INF;
    int completed_depth = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        if (should_stop_now(&search)) {
            break;
        }
        Move iteration_move = best;
        int iteration_score = -REVERSI_AI_INF;
        if (!root_search(&search, state, &bb, root_hash, depth, best,
                         completed_depth > 0, &iteration_move,
                         &iteration_score)) {
            break;
        }
        best = iteration_move;
        best_score = iteration_score;
        completed_depth = depth;

        if (best_score >= REVERSI_AI_WIN_THRESHOLD
            || best_score <= -REVERSI_AI_WIN_THRESHOLD) {
            break;
        }
        if (empty <= 12 && depth >= empty) {
            break;
        }
    }

    finish_stats(&search, completed_depth, best_score, out_stats);
    free(search.tt);

    if (completed_depth == 0) {
        return false;
    }
    if (!rules_is_legal_move(&state->board, state->current_player, best)) {
        return false;
    }

    *out_move = best;
    return true;
}

bool ai_search_choose(const GameState *state, const AiConfig *config,
                      Move *out_move, AiStats *out_stats)
{
    return search_choose(state, config, true, out_move, out_stats);
}

bool ai_search_choose_ex(const GameState *state, const AiConfig *config,
                         bool use_tt, Move *out_move, AiStats *out_stats)
{
    return search_choose(state, config, use_tt, out_move, out_stats);
}

bool ai_easy_choose(const GameState *state, const AiConfig *config,
                    Move *out_move, AiStats *out_stats)
{
    if (state == NULL || config == NULL || out_move == NULL
        || !ai_config_is_valid(config)) {
        return false;
    }

    Bitboard bb;
    bitboard_from_board(&state->board, &bb);
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = moves_from_bits(
        bitboard_legal_moves(&bb, state->current_player), moves);
    if (count == 0) {
        return false;
    }

    const uint64_t start = ai_now_ms();
    uint64_t rng = config->seed != 0
        ? (uint64_t)config->seed : 0x9E3779B97F4A7C15ULL;
    int best_score = INT_MIN;
    Move best = moves[0];

    for (int i = 0; i < count; ++i) {
        GameState child;
        Bitboard child_bb;
        if (!apply_search_move(NULL, &child, &child_bb, NULL, state, &bb, 0,
                               moves[i])) {
            continue;
        }
        AiEvaluation evaluation;
        ai_evaluate_for_difficulty(&child, config->difficulty, &evaluation);
        int score = -evaluation.total;
        if (config->randomness > 0) {
            const int span = config->randomness * 2 + 1;
            score += (int)(rng_next(&rng) % (uint64_t)span)
                - config->randomness;
        }
        if (score > best_score) {
            best_score = score;
            best = moves[i];
        }
    }

    if (out_stats != NULL) {
        *out_stats = (AiStats){
            .nodes = (uint64_t)count,
            .cutoffs = 0,
            .tt_hits = 0,
            .depth = 1,
            .score = best_score,
            .elapsed_ms = (int)(ai_now_ms() - start),
            .cancelled = false,
            .timed_out = false
        };
    }

    if (!rules_is_legal_move(&state->board, state->current_player, best)) {
        return false;
    }
    *out_move = best;
    return true;
}
