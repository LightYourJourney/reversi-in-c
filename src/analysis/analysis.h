#ifndef REVERSI_ANALYSIS_ANALYSIS_H
#define REVERSI_ANALYSIS_ANALYSIS_H

#include "core/game.h"
#include "core/history.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Reversi Lab analysis tree.
 *
 * An AnalysisSession is deliberately separate from Match and History. It owns
 * a tree of positions and never mutates a live Match. Every position is a
 * complete GameState, so analysis can start from the standard opening, from a
 * finished game, or from a manually edited board.
 *
 * Positions are stored in a fixed pool. Children are linked in creation order;
 * the first child is the main line. Playing a move that already exists under
 * the current node navigates to that child instead of duplicating it.
 */

#define ANALYSIS_MAX_POSITIONS 512
#define ANALYSIS_MAX_PLIES 64
#define ANALYSIS_MAX_VARIATIONS 8
#define ANALYSIS_MAX_RECORD_BYTES 8192

typedef enum AnalysisPositionKind {
    /* Reachable from the standard opening by legal moves (or by the
       session's imported starting point, which is also a legal game). */
    ANALYSIS_POSITION_VALID = 0,
    /* Built by the board editor. It has no standard-opening history, so
       analysis must not claim that its moves form a legal game record. */
    ANALYSIS_POSITION_CUSTOM
} AnalysisPositionKind;

/* A move (or an explicit pass) that leads from one position to another. */
typedef struct AnalysisMove {
    Move move;
    Stone mover;
    bool is_pass;
    /* True when the opponent had no legal move and was skipped after the
       move that led here. Recorded so exports can preserve pass events. */
    bool opponent_passed;
} AnalysisMove;

typedef struct AnalysisPosition {
    GameState state;
    AnalysisPositionKind kind;
    int parent;
    int first_child;
    int next_sibling;
    int ply;
    AnalysisMove via;
    bool has_via;
} AnalysisPosition;

/*
 * A branch view: the children of `start` other than the main child, or the
 * main child itself when `child_index` is 0. The line runs from `start` along
 * first children to `end`.
 */
typedef struct AnalysisVariation {
    int start;
    int end;
    int length;
    int child_index;
} AnalysisVariation;

/* Result summary for the current position. */
typedef struct AnalysisResult {
    GameResult result;
    int black_discs;
    int white_discs;
    bool terminal;
} AnalysisResult;

typedef struct AnalysisSession {
    AnalysisPosition positions[ANALYSIS_MAX_POSITIONS];
    int count;
    int root;
    int current;
} AnalysisSession;

/* Resets to the standard opening with Black to move (VALID root). */
void analysis_init(AnalysisSession *session);

/* Resets to an empty board with the given side to move (CUSTOM root). */
void analysis_reset_custom(AnalysisSession *session, Stone to_move);

/*
 * Resets the whole tree to the supplied position. The caller states whether
 * the position is reachable from the standard opening; passing VALID for an
 * arbitrary board is not validated here, so callers should use it only for
 * positions reached through analysis_import_history()/load.
 */
bool analysis_load_state(AnalysisSession *session, const GameState *state,
                         AnalysisPositionKind kind);

/*
 * Imports the recorded moves of a history into a fresh tree. Requires entry
 * 0 to be the standard opening. Explicit pass events are replayed as passes.
 * On failure the destination is unchanged.
 */
bool analysis_import_history(AnalysisSession *session, const History *history);

/* Current position accessors. */
const AnalysisPosition *analysis_current(const AnalysisSession *session);
const AnalysisPosition *analysis_position(const AnalysisSession *session,
                                          int index);
bool analysis_current_playable(const AnalysisSession *session);
AnalysisPositionKind analysis_current_kind(const AnalysisSession *session);
AnalysisResult analysis_current_result(const AnalysisSession *session);

/*
 * Plays a legal move from the current position and moves to the resulting
 * child, creating a branch when the move is not an existing child. Returns
 * false for illegal moves, a finished game, or a full pool.
 */
bool analysis_play_move(AnalysisSession *session, Move move);

/* Explicitly passes; only valid when the current side must pass. */
bool analysis_pass(AnalysisSession *session);

/* Navigation inside the tree and along sibling branches. */
bool analysis_goto(AnalysisSession *session, int index);
bool analysis_first(AnalysisSession *session);
bool analysis_prev(AnalysisSession *session);
bool analysis_next(AnalysisSession *session);
bool analysis_last(AnalysisSession *session);
bool analysis_return_to_main(AnalysisSession *session);
bool analysis_next_variation(AnalysisSession *session);
int analysis_variation_count(const AnalysisSession *session);
bool analysis_variation_at(const AnalysisSession *session, int index,
                           AnalysisVariation *out);

/*
 * Board editor. Every edit discards the tree and makes the edited board a
 * CUSTOM root: a manually edited board has no legal history, so keeping the
 * old branches would imply a record that never existed. Callers should ask
 * the user before the first edit.
 */
bool analysis_edit_cell(AnalysisSession *session, int row, int col,
                        Stone stone);
bool analysis_clear_board(AnalysisSession *session);
bool analysis_set_standard(AnalysisSession *session);
bool analysis_set_turn(AnalysisSession *session, Stone player);
bool analysis_swap_colors(AnalysisSession *session);

#endif
