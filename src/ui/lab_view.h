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
    LAB_BTN_COPY,
    LAB_BTN_PASTE,
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
    Rectangle candidate[AI_ANALYSIS_MAX_LINES];
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
    int batch_nodes[ANALYSIS_MAX_PLIES + 1];

    bool score_valid[ANALYSIS_MAX_PLIES + 1];
    float score_values[ANALYSIS_MAX_PLIES + 1];
    bool marks[ANALYSIS_MAX_PLIES + 1];
    Move best_moves[ANALYSIS_MAX_PLIES + 1];
    bool best_valid[ANALYSIS_MAX_PLIES + 1];
    int review_depth[ANALYSIS_MAX_PLIES + 1];
    bool review_complete[ANALYSIS_MAX_PLIES + 1];
    bool review_exact[ANALYSIS_MAX_PLIES + 1];

    bool edit_mode;
    Stone edit_stone;
    bool auto_play;
    float auto_timer;
    int speed_index;
    int selected_line;
    bool has_match;
    bool focus_visible;
    int focus;

    char notice[160];
    float notice_timer;

    LabLayout layout;
    UiButton buttons[LAB_BTN_COUNT];
    bool clicked[LAB_BTN_COUNT];
} LabView;

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
