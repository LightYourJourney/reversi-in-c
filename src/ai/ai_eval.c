#include "ai/ai.h"

#include "core/rules.h"

#include <stddef.h>

/*
 * Positional table for non-corner squares. Corners are scored separately so
 * they are not counted twice. X-squares (-25) and C-squares (-50) are
 * deliberately expensive while the adjacent corner is still empty; the table
 * is static and symmetric.
 */
static const int AI_POSITIONAL[REVERSI_BOARD_SIZE][REVERSI_BOARD_SIZE] = {
    {  0, -25,  10,   5,   5,  10, -25,   0 },
    {-25, -50,  -5,  -5,  -5,  -5, -50, -25 },
    { 10,  -5,   1,   1,   1,   1,  -5,  10 },
    {  5,  -5,   1,   0,   0,   1,  -5,   5 },
    {  5,  -5,   1,   0,   0,   1,  -5,   5 },
    { 10,  -5,   1,   1,   1,   1,  -5,  10 },
    {-25, -50,  -5,  -5,  -5,  -5, -50, -25 },
    {  0, -25,  10,   5,   5,  10, -25,   0 }
};

typedef struct AiWeights {
    int corners;
    int mobility;
    int potential;
    int positional;
    int stability;
    int frontier;
    int discs;
    int parity;
} AiWeights;

/*
 * Phase weights. The opening values mobility and corners; the endgame shifts
 * toward exact disc count and edge stability. Disc count is deliberately zero
 * in the opening because taking discs early usually gives the opponent moves.
 */
static const AiWeights AI_WEIGHTS[3] = {
    { 100, 30, 12,  8,  8, 10,  0, 0 }, /* opening */
    { 120, 25, 12, 10, 14, 10,  2, 2 }, /* midgame */
    { 140,  8,  5,  8, 18,  6, 12, 0 }  /* endgame */
};

static const int AI_TERMINAL_WIN = 100000;

static bool is_corner(int row, int col)
{
    return (row == 0 || row == REVERSI_BOARD_SIZE - 1)
        && (col == 0 || col == REVERSI_BOARD_SIZE - 1);
}

int ai_positional_value(int row, int col)
{
    if (!board_in_bounds(row, col)) {
        return 0;
    }
    return AI_POSITIONAL[row][col];
}

static AiPhase phase_for_empty(int empty_cells)
{
    if (empty_cells >= 44) {
        return AI_PHASE_OPENING;
    }
    if (empty_cells >= 16) {
        return AI_PHASE_MIDGAME;
    }
    return AI_PHASE_ENDGAME;
}

/*
 * Conservative edge-stability heuristic: only edge discs connected to an
 * owned corner through a contiguous run of the same colour are counted.
 * Interior stability is not modelled (the evaluation is a heuristic; the
 * endgame score is exact at terminal nodes).
 */
static int edge_stable_count(const Board *board, Stone player)
{
    int total = 0;
    int rows[REVERSI_BOARD_SIZE];
    int cols[REVERSI_BOARD_SIZE];

    for (int edge = 0; edge < 4; ++edge) {
        for (int i = 0; i < REVERSI_BOARD_SIZE; ++i) {
            switch (edge) {
            case 0: /* top */
                rows[i] = 0;
                cols[i] = i;
                break;
            case 1: /* bottom */
                rows[i] = REVERSI_BOARD_SIZE - 1;
                cols[i] = i;
                break;
            case 2: /* left */
                rows[i] = i;
                cols[i] = 0;
                break;
            default: /* right */
                rows[i] = i;
                cols[i] = REVERSI_BOARD_SIZE - 1;
                break;
            }
        }

        bool counted[REVERSI_BOARD_SIZE] = {false};
        if (board_get(board, rows[0], cols[0]) == player) {
            for (int i = 1; i < REVERSI_BOARD_SIZE
                 && board_get(board, rows[i], cols[i]) == player; ++i) {
                counted[i] = true;
            }
        }
        if (board_get(board, rows[REVERSI_BOARD_SIZE - 1],
                      cols[REVERSI_BOARD_SIZE - 1]) == player) {
            for (int i = REVERSI_BOARD_SIZE - 2;
                 i >= 0 && board_get(board, rows[i], cols[i]) == player;
                 --i) {
                counted[i] = true;
            }
        }

        for (int i = 1; i < REVERSI_BOARD_SIZE - 1; ++i) {
            if (counted[i]) {
                ++total;
            }
        }
    }
    return total;
}

static int frontier_count(const Board *board, Stone player)
{
    int count = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board_get(board, row, col) != player || is_corner(row, col)) {
                continue;
            }
            bool touches_empty = false;
            for (int dr = -1; dr <= 1 && !touches_empty; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) {
                        continue;
                    }
                    const int nr = row + dr;
                    const int nc = col + dc;
                    if (board_in_bounds(nr, nc)
                        && board_get(board, nr, nc) == STONE_EMPTY) {
                        touches_empty = true;
                        break;
                    }
                }
            }
            if (touches_empty) {
                ++count;
            }
        }
    }
    return count;
}

static int potential_mobility_count(const Board *board, Stone player)
{
    const Stone opponent = stone_opponent(player);
    int count = 0;

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board_get(board, row, col) != STONE_EMPTY) {
                continue;
            }
            bool adjacent = false;
            for (int dr = -1; dr <= 1 && !adjacent; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) {
                        continue;
                    }
                    if (board_get(board, row + dr, col + dc) == opponent) {
                        adjacent = true;
                        break;
                    }
                }
            }
            if (adjacent) {
                ++count;
            }
        }
    }
    return count;
}

static int positional_score(const Board *board, Stone player)
{
    int score = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board_get(board, row, col);
            if (stone == player) {
                score += AI_POSITIONAL[row][col];
            } else if (stone == stone_opponent(player)) {
                score -= AI_POSITIONAL[row][col];
            }
        }
    }
    return score;
}

static int corner_count(const Board *board, Stone player)
{
    int count = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; row += REVERSI_BOARD_SIZE - 1) {
        for (int col = 0; col < REVERSI_BOARD_SIZE;
             col += REVERSI_BOARD_SIZE - 1) {
            if (board_get(board, row, col) == player) {
                ++count;
            }
        }
    }
    return count;
}

bool ai_is_terminal(const GameState *state)
{
    if (state == NULL) {
        return false;
    }
    return rules_count_legal_moves(&state->board, STONE_BLACK) == 0
        && rules_count_legal_moves(&state->board, STONE_WHITE) == 0;
}

int ai_terminal_score(const GameState *state)
{
    if (state == NULL || !stone_is_player(state->current_player)) {
        return 0;
    }

    const int black = board_count(&state->board, STONE_BLACK);
    const int white = board_count(&state->board, STONE_WHITE);
    if (black == white) {
        return 0;
    }

    const bool current_is_black = state->current_player == STONE_BLACK;
    const bool current_wins = current_is_black ? black > white : white > black;
    const int difference = current_is_black ? black - white : white - black;
    return (current_wins ? AI_TERMINAL_WIN : -AI_TERMINAL_WIN) + difference;
}

void ai_evaluate(const GameState *state, AiEvaluation *out)
{
    if (out == NULL) {
        return;
    }
    *out = (AiEvaluation){0};
    if (state == NULL || !stone_is_player(state->current_player)) {
        return;
    }

    const Board *board = &state->board;
    const Stone own = state->current_player;
    const Stone opponent = stone_opponent(own);
    const int black = board_count(board, STONE_BLACK);
    const int white = board_count(board, STONE_WHITE);
    const int empty = REVERSI_BOARD_CELL_COUNT - black - white;
    const AiPhase phase = phase_for_empty(empty);
    const AiWeights *weights = &AI_WEIGHTS[phase];

    out->phase = phase;
    out->empty_cells = empty;
    out->corners = corner_count(board, own) - corner_count(board, opponent);
    out->mobility = rules_count_legal_moves(board, own)
        - rules_count_legal_moves(board, opponent);
    out->potential_mobility = potential_mobility_count(board, own)
        - potential_mobility_count(board, opponent);
    out->positional = positional_score(board, own);
    out->stability = edge_stable_count(board, own)
        - edge_stable_count(board, opponent);
    out->frontier = frontier_count(board, opponent)
        - frontier_count(board, own);
    out->discs = (own == STONE_BLACK ? black - white : white - black);
    out->parity = (empty % 2 == 1) ? 1 : -1;

    out->total = weights->corners * out->corners
        + weights->mobility * out->mobility
        + weights->potential * out->potential_mobility
        + weights->positional * out->positional
        + weights->stability * out->stability
        + weights->frontier * out->frontier
        + weights->discs * out->discs
        + weights->parity * out->parity;
}
