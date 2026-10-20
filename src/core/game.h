#ifndef REVERSI_CORE_GAME_H
#define REVERSI_CORE_GAME_H

#include "core/board.h"

#include <stdbool.h>

typedef enum GameResult {
    GAME_RESULT_ONGOING = 0,
    GAME_RESULT_BLACK_WIN = 1,
    GAME_RESULT_WHITE_WIN = 2,
    GAME_RESULT_DRAW = 3
} GameResult;

typedef struct GameState {
    Board board;
    Stone current_player;
    GameResult result;
} GameState;

/*
 * Initializes the standard opening with Black to move and result ONGOING.
 * NULL is ignored.
 */
void game_init(GameState *state);

/*
 * Applies a move for current_player. Returns false and leaves the state
 * unchanged when the game is over or the move is illegal.
 *
 * After a legal move the opponent moves next. If the opponent has no legal
 * move but the mover does, the opponent's turn is automatically skipped and
 * current_player remains the mover. If neither player can move, the game
 * ends and result is set from the final stone counts.
 */
bool game_apply_move(GameState *state, Move move);

/*
 * Explicitly passes the turn. Only valid when current_player has no legal
 * move; returns false and leaves the state unchanged otherwise (including
 * when the game is over). If the opponent also has no move, the game ends.
 */
bool game_pass(GameState *state);

/* Game-over and result queries. Invalid state returns false/ONGOING. */
bool game_is_over(const GameState *state);
GameResult game_result(const GameState *state);

/* True when the current player must pass (and the game is not over). */
bool game_must_pass(const GameState *state);

/*
 * Checks internal invariants: valid board, valid current player, an ongoing
 * game where at least one player has a move, and a terminal result consistent
 * with the final counts and the absence of legal moves.
 */
bool game_is_consistent(const GameState *state);

/* Board, current player and result equality. NULL only equals NULL. */
bool game_state_equal(const GameState *a, const GameState *b);

#endif
