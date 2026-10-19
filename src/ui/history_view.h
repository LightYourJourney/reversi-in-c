#ifndef REVERSI_UI_HISTORY_VIEW_H
#define REVERSI_UI_HISTORY_VIEW_H

#include "core/match.h"
#include "ui/draw.h"

typedef enum HistoryAction {
    HISTORY_ACTION_NONE = 0,
    HISTORY_ACTION_BACK
} HistoryAction;

typedef struct HistoryView {
    int scroll;
    UiButton back;
} HistoryView;

void history_view_init(HistoryView *view);
void history_view_open(HistoryView *view, const Match *match);
HistoryAction history_view_update(HistoryView *view, const Match *match,
                                  const UiInput *input,
                                  bool accept_input, float dt);
void history_view_draw(const HistoryView *view, const Match *match,
                       const UiFonts *fonts);

#endif
