#include "ai/ai_endgame.h"

#include "ai/ai_shared.h"
#include "ai/ai_internal.h"
#include "core/bitboard.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_ENDGAME_TT_SIZE (1u << 16)
#define REVERSI_ENDGAME_CHECK_MASK 1023u

typedef enum EndgameFlag {
    ENDGAME_TT_EXACT = 0,
    ENDGAME_TT_LOWER,
    ENDGAME_TT_UPPER
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
    AiZobrist keys;
    EndgameTtEntry *tt;
    uint32_t tt_mask;
} EndgameSearch;

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
        && ai_now_ms() - search->start_ms
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
        && ai_now_ms() - search->start_ms
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
                           hash ^ search->keys.turn[0] ^ search->keys.turn[1],
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
                if (entry->flag == ENDGAME_TT_EXACT) {
                    return entry->score;
                }
                if (entry->flag == ENDGAME_TT_LOWER && entry->score > alpha) {
                    alpha = entry->score;
                } else if (entry->flag == ENDGAME_TT_UPPER
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
    const int count = ai_moves_from_bits(legal, moves);
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
            ai_zobrist_after_move(&search->keys, hash, player, index, flips);
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
            entry->flag = ENDGAME_TT_UPPER;
        } else if (best >= beta_window) {
            entry->flag = ENDGAME_TT_LOWER;
        } else {
            entry->flag = ENDGAME_TT_EXACT;
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
    search.start_ms = ai_now_ms();
    ai_zobrist_init(&search.keys);
    if (empties >= 8) {
        search.tt = calloc(REVERSI_ENDGAME_TT_SIZE, sizeof(EndgameTtEntry));
        search.tt_mask = REVERSI_ENDGAME_TT_SIZE - 1u;
    }

    const uint64_t hash = ai_zobrist_position(&search.keys, bb.black,
                                              bb.white, state->current_player);
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = ai_moves_from_bits(legal, moves);
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
        const uint64_t child_hash = ai_zobrist_after_move(
            &search.keys, hash, state->current_player, index, flips);
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
            (int)(ai_now_ms() - search.start_ms);
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
