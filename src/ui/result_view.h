#ifndef REVERSI_UI_RESULT_VIEW_H
#define REVERSI_UI_RESULT_VIEW_H

#include "core/match.h"
#include "ui/draw.h"

typedef enum ResultAction {
    RESULT_ACTION_NONE = 0,
    RESULT_ACTION_PLAY_AGAIN,
    RESULT_ACTION_UNDO,
    RESULT_ACTION_MENU
} ResultAction;

typedef struct ResultView {
    UiButton again;
    UiButton undo;
    UiButton menu;
    int focus;
    bool focus_visible;
    float appear;
} ResultView;

void result_view_init(ResultView *view);
void result_view_reset(ResultView *view);
ResultAction result_view_update(ResultView *view, const UiInput *input,
                                bool accept_input, bool animations,
                                bool can_undo, float dt);
void result_view_draw(const ResultView *view, const UiFonts *fonts,
                      const Match *match);

#endif
