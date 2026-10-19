#ifndef REVERSI_AI_AI_H
#define REVERSI_AI_AI_H

#include "core/game.h"
#include "core/match.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum AiDifficulty {
    AI_DIFFICULTY_EASY = 0,
    AI_DIFFICULTY_MEDIUM,
    AI_DIFFICULTY_HARD
} AiDifficulty;

typedef enum AiPhase {
    AI_PHASE_OPENING = 0,
    AI_PHASE_MIDGAME,
    AI_PHASE_ENDGAME
} AiPhase;

/*
 * Component scores are always from the perspective of the side to move:
 * own value minus opponent value. The total is the weighted sum below.
 */
typedef struct AiEvaluation {
    AiPhase phase;
    int empty_cells;
    int corners;
    int mobility;
    int potential_mobility;
    int positional;
    int stability;
    int frontier;
    int discs;
    int parity;
    int total;
} AiEvaluation;

/* Called periodically by a search; returning true aborts the search. */
typedef bool (*AiCancelFn)(void *context);

typedef struct AiConfig {
    AiDifficulty difficulty;
    int max_depth;
    int time_budget_ms;
    int randomness;
    unsigned seed;
    AiCancelFn should_cancel;
    void *cancel_context;
} AiConfig;

typedef struct AiStats {
    uint64_t nodes;
    uint64_t cutoffs;
    uint64_t tt_hits;
    int depth;
    int score;
    int elapsed_ms;
    bool cancelled;
    bool timed_out;
} AiStats;

/*
 * Defaults for each difficulty. The seed only affects Easy's tie-breaking.
 * Time budgets are soft: the search checks the clock every 1024 nodes.
 */
AiConfig ai_default_config(AiDifficulty difficulty, unsigned seed);

bool ai_config_is_valid(const AiConfig *config);

/*
 * Evaluates the position for the side to move. ai_is_terminal() reports
 * whether neither side has a legal move; terminal scores are exact.
 */
void ai_evaluate(const GameState *state, AiEvaluation *out);
bool ai_is_terminal(const GameState *state);
int ai_terminal_score(const GameState *state);

/* Static square value used by both evaluation and move ordering. */
int ai_positional_value(int row, int col);

const char *ai_difficulty_name(AiDifficulty difficulty);
const char *ai_phase_name(AiPhase phase);

/*
 * Chooses a move for the side to move. Never returns an illegal move: the
 * result is re-checked against the rules before returning. Returns false when
 * the state has no legal move, the configuration is invalid, or a cancel
 * callback aborts the search before a complete depth.
 *
 * The state is read-only; the search works on copies. out_stats is optional.
 */
bool ai_choose_move(const GameState *state, const AiConfig *config,
                    Move *out_move, AiStats *out_stats);

/*
 * Synchronous controllers for MatchConfig.ai. The app uses ai_choose_move()
 * with its own config and seed; these exist for tests and configuration
 * validation.
 */
const AiController *ai_controller_for_difficulty(AiDifficulty difficulty);

#endif
