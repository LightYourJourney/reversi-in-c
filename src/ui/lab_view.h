#ifndef REVERSI_UI_LAB_VIEW_H
#define REVERSI_UI_LAB_VIEW_H

#include "ai/ai_analyze.h"
#include "ai/ai_worker.h"
#include "analysis/analysis.h"
#include "core/match.h"
#include "ui/draw.h"

#include <raylib.h>
#include <stdbool.h>

typedef enum LabAction {
    LAB_ACTION_NONE = 0,
    LAB_ACTION_BACK
} LabAction;

typedef enum LabButtonIndex {
    LAB_BTN_BACK = 0,
    LAB_BTN_LOAD_GAME,
    LAB_BTN_SAVE_RECORD,
    LAB_BTN_LOAD_RECORD,
    LAB_BTN_FIRST,
    LAB_BTN_PREV,
    LAB_BTN_NEXT,
    LAB_BTN_LAST,
    LAB_BTN_PLAY,
    LAB_BTN_SPEED,
    LAB_BTN_PASS,
    LAB_BTN_ANALYZE,
    LAB_BTN_STOP,
    LAB_BTN_ANALYZE_ALL,
    LAB_BTN_MAIN,
    LAB_BTN_EDIT,
    LAB_BTN_BLACK,
    LAB_BTN_WHITE,
    LAB_BTN_ERASE,
    LAB_BTN_CLEAR,
    LAB_BTN_STANDARD,
    LAB_BTN_TURN,
    LAB_BTN_SWAP,
    LAB_BTN_DONE,
    LAB_BTN_COUNT
} LabButtonIndex;

typedef struct LabLayout {
    bool wide;
    Rectangle top;
    Rectangle board;
    float cell;
    Rectangle panel;
    Rectangle bottom;
    Rectangle candidate[REVERSI_AI_ANALYSIS_MAX_LINES];
    Rectangle graph;
} LabLayout;

typedef struct LabView {
    AnalysisSession session;
    AiAnalysis analysis;
    bool has_analysis;
    bool analyzing;
    unsigned position_generation;
    unsigned requested_generation;
    int pending_batch_index;

    bool batch_running;
    int batch_index;
    int batch_count;
    int batch_nodes[REVERSI_ANALYSIS_MAX_PLIES + 1];

    bool score_valid[REVERSI_ANALYSIS_MAX_PLIES + 1];
    float score_values[REVERSI_ANALYSIS_MAX_PLIES + 1];
    bool marks[REVERSI_ANALYSIS_MAX_PLIES + 1];
    Move best_moves[REVERSI_ANALYSIS_MAX_PLIES + 1];
    bool best_valid[REVERSI_ANALYSIS_MAX_PLIES + 1];
    int review_depth[REVERSI_ANALYSIS_MAX_PLIES + 1];
    bool review_complete[REVERSI_ANALYSIS_MAX_PLIES + 1];
    bool review_exact[REVERSI_ANALYSIS_MAX_PLIES + 1];

    /* Scores keyed by node, so a branch analysis never overwrites the
       main-line curve. */
    float node_scores[REVERSI_ANALYSIS_MAX_POSITIONS];
    bool node_score_valid[REVERSI_ANALYSIS_MAX_POSITIONS];
    int node_score_depth[REVERSI_ANALYSIS_MAX_POSITIONS];
    bool node_score_complete[REVERSI_ANALYSIS_MAX_POSITIONS];
    bool node_score_exact[REVERSI_ANALYSIS_MAX_POSITIONS];

    bool edit_mode;
    Stone edit_stone;
    bool auto_play;
    float auto_timer;
    int speed_index;
    int selected_line;
    bool has_match;

    char notice[160];
    float notice_timer;

    LabLayout layout;
    UiButton buttons[LAB_BTN_COUNT];
    bool clicked[LAB_BTN_COUNT];
} LabView;

/* True only when a finished search belongs to the current position. */
bool lab_view_accepts_result(unsigned requested_generation,
                             unsigned current_generation);

void lab_view_init(LabView *view);

/*
 * Starts a fresh session: the current match's recorded history when one is
 * available, otherwise the standard opening. Does not touch the match.
 */
void lab_view_enter(LabView *view, const Match *match, bool has_match);

/* Cancels any running analysis and joins the worker. */
void lab_view_leave(LabView *view, AiWorker *worker);

LabAction lab_view_update(LabView *view, const UiInput *input, float dt,
                          AiWorker *worker, const Match *match,
                          bool has_match);

void lab_view_draw(const LabView *view, const UiFonts *fonts);

#endif
