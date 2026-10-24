#ifndef REVERSI_AI_AI_ANALYZE_H
#define REVERSI_AI_AI_ANALYZE_H

#include "ai/ai.h"

#include <stdbool.h>

#define AI_ANALYSIS_MAX_LINES 3
#define AI_ANALYSIS_MAX_PV 16

/*
 * One root candidate. Lines at the same reported depth were searched with the
 * same full window, so their scores are comparable. When the search is cut
 * mid-depth, the whole depth is discarded and the previous depth is reported
 * instead; complete is false only in that case (or when the depth limit was
 * reached before max_depth).
 */
typedef struct AiAnalysisLine {
    Move move;
    int score;
    int depth;
    bool complete;
    bool exact;
    int pv_length;
    Move pv[AI_ANALYSIS_MAX_PV];
} AiAnalysisLine;

typedef struct AiAnalysis {
    bool complete;
    /* True only when the line is a fully solved endgame score. */
    bool exact;
    int depth;
    int line_count;
    AiAnalysisLine lines[AI_ANALYSIS_MAX_LINES];
    AiStats stats;
} AiAnalysis;

/*
 * Read-only MultiPV analysis. Does not call or modify the move-choosing path
 * used by matches, and never mutates the input state.
 *
 * max_lines is clamped to [1, AI_ANALYSIS_MAX_LINES]. PV entries are
 * reconstructed from the transposition table; each is replayed through the
 * rules before being reported, and the PV stops at a TT miss, a pass, or the
 * length limit. Scores are from the side to move. Returns false when the
 * position is terminal, has no legal move, or no depth completed.
 */
bool ai_analyze_position(const GameState *state, const AiConfig *config,
                         int max_lines, AiAnalysis *out);

/*
 * Converts an evaluation from the side-to-move perspective to Black-positive
 * values (positive means Black is better). Used by the analysis UI so every
 * score shares one convention.
 */
void ai_evaluation_to_black(const AiEvaluation *side_to_move,
                            Stone current_player, AiEvaluation *out_black);

#endif
