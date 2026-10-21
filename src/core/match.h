#ifndef REVERSI_CORE_MATCH_H
#define REVERSI_CORE_MATCH_H

#include "core/history.h"

#include <stdbool.h>

typedef enum GameMode {
    GAME_MODE_PVP = 0,
    GAME_MODE_PVE
} GameMode;

/*
 * Reserved AI boundary for Phase 4. The controller reads a snapshot and
 * returns one candidate move; the match still validates it through the rules.
 * The controller is not owned by Match.
 */
typedef struct AiController {
    const char *name;
    bool (*select_move)(void *context, const GameState *state, Move *out_move);
    void *context;
} AiController;

typedef enum MatchPhase {
    MATCH_PHASE_PLAYER_TURN = 0,
    MATCH_PHASE_ANIMATING,
    MATCH_PHASE_PASSING_TURN,
    MATCH_PHASE_PAUSED,
    MATCH_PHASE_GAME_OVER,
    MATCH_PHASE_AI_THINKING
} MatchPhase;

typedef struct MatchConfig {
    GameMode mode;
    Stone human_color;
    const AiController *ai;
} MatchConfig;

typedef struct Match {
    GameState state;
    History history;
    MatchPhase phase;
    MatchConfig config;
    Move last_move;
    Stone last_mover;
    int last_flips;
    bool has_last_move;
    PassEvent last_pass;
} Match;

/* Starts a standard opening. PVE requires a non-NULL AI controller. */
bool match_start(Match *match, const MatchConfig *config);

/*
 * Starts from a given position (for tests and future save/load). The position
 * must be consistent; an ongoing position must have a move for current_player.
 */
bool match_start_from(Match *match, const MatchConfig *config,
                      const GameState *initial);

/*
 * Restores a match by replaying a move sequence from the standard opening.
 * Every move must be legal; the resulting phase is PLAYER_TURN or GAME_OVER.
 * On failure the destination is left unchanged.
 */
bool match_restore(Match *match, const MatchConfig *config,
                   const Move *moves, int count);

/* As match_restore, but also requires the replayed state to equal expected. */
bool match_restore_checked(Match *match, const MatchConfig *config,
                           const Move *moves, int count,
                           const GameState *expected);

/* Applies a move in PLAYER_TURN and enters ANIMATING. */
bool match_apply_move(Match *match, Move move);

/* ANIMATING -> PLAYER_TURN / PASSING_TURN / GAME_OVER. */
bool match_finish_animation(Match *match);

/* PASSING_TURN -> PLAYER_TURN (or GAME_OVER if the position is terminal). */
bool match_finish_pass(Match *match);

bool match_pause(Match *match);
bool match_resume(Match *match);

/*
 * PVE only: enters AI_THINKING when it is the computer's turn and it has at
 * least one legal move. Leaves the board untouched; the caller owns the
 * actual search. match_apply_move() accepts a move in AI_THINKING.
 */
bool match_begin_ai_turn(Match *match);

/* AI_THINKING -> PLAYER_TURN, e.g. when a search is cancelled. */
bool match_cancel_ai_turn(Match *match);

/*
 * PVP undoes one move. PVE undoes until the human player's previous decision
 * point (or the start of the history). Redo restores the next decision point;
 * when the recorded branch ends immediately after the human's move, redo
 * restores that position and the AI is to move.
 */
bool match_undo(Match *match);
bool match_redo(Match *match);

/* Restarts the current mode/color from the standard opening. */
bool match_restart(Match *match);

bool match_is_active(const Match *match);
bool match_is_human_turn(const Match *match);
bool match_can_undo(const Match *match);
bool match_can_redo(const Match *match);
bool match_is_consistent(const Match *match);

#endif
