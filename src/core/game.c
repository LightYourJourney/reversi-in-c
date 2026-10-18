#include "core/game.h"

#include "core/rules.h"

#include <stddef.h>

static bool player_is_valid(Stone player)
{
    return player == STONE_BLACK || player == STONE_WHITE;
}

static GameResult result_from_counts(const Board *board)
{
    const int black = board_count(board, STONE_BLACK);
    const int white = board_count(board, STONE_WHITE);

    if (black > white) {
        return GAME_RESULT_BLACK_WIN;
    }
    if (white > black) {
        return GAME_RESULT_WHITE_WIN;
    }
    return GAME_RESULT_DRAW;
}

void game_init(GameState *state)
{
    if (state == NULL) {
        return;
    }

    board_init(&state->board);
    state->current_player = STONE_BLACK;
    state->result = GAME_RESULT_ONGOING;
}

bool game_apply_move(GameState *state, Move move)
{
    if (state == NULL || state->result != GAME_RESULT_ONGOING) {
        return false;
    }
    if (!player_is_valid(state->current_player)) {
        return false;
    }

    const Stone mover = state->current_player;
    const int flips = rules_apply_move(&state->board, mover, move);
    if (flips <= 0) {
        return false;
    }

    const Stone next = stone_opponent(mover);
    state->current_player = next;

    if (board_is_full(&state->board)) {
        state->result = result_from_counts(&state->board);
        return true;
    }

    if (rules_count_legal_moves(&state->board, next) == 0) {
        if (rules_count_legal_moves(&state->board, mover) > 0) {
            state->current_player = mover;
        } else {
            state->result = result_from_counts(&state->board);
        }
    }

    return true;
}

bool game_pass(GameState *state)
{
    if (state == NULL || state->result != GAME_RESULT_ONGOING) {
        return false;
    }
    if (!player_is_valid(state->current_player)) {
        return false;
    }
    if (rules_count_legal_moves(&state->board, state->current_player) > 0) {
        return false;
    }

    const Stone next = stone_opponent(state->current_player);
    state->current_player = next;

    if (rules_count_legal_moves(&state->board, next) == 0) {
        state->result = result_from_counts(&state->board);
    }

    return true;
}

bool game_is_over(const GameState *state)
{
    return state != NULL && state->result != GAME_RESULT_ONGOING;
}

GameResult game_result(const GameState *state)
{
    if (state == NULL) {
        return GAME_RESULT_ONGOING;
    }
    return state->result;
}

bool game_must_pass(const GameState *state)
{
    if (state == NULL || state->result != GAME_RESULT_ONGOING) {
        return false;
    }
    if (!player_is_valid(state->current_player)) {
        return false;
    }
    return rules_count_legal_moves(&state->board, state->current_player) == 0;
}

bool game_is_consistent(const GameState *state)
{
    if (state == NULL || !board_is_valid(&state->board)) {
        return false;
    }
    if (!player_is_valid(state->current_player)) {
        return false;
    }

    const bool current_has_move =
        rules_count_legal_moves(&state->board, state->current_player) > 0;
    const bool opponent_has_move =
        rules_count_legal_moves(&state->board,
                                stone_opponent(state->current_player)) > 0;
    const bool full = board_is_full(&state->board);

    if (state->result == GAME_RESULT_ONGOING) {
        if (full) {
            return false;
        }
        /* A current player with no move is a valid must-pass state as long
         * as the opponent can still move; both having no move is terminal. */
        return current_has_move || opponent_has_move;
    }

    if (!full && (current_has_move || opponent_has_move)) {
        return false;
    }

    return state->result == result_from_counts(&state->board);
}
