#ifndef REVERSI_UI_PAUSE_VIEW_H
#define REVERSI_UI_PAUSE_VIEW_H

#include "ui/draw.h"

typedef enum PauseAction {
    PAUSE_ACTION_NONE = 0,
    PAUSE_ACTION_RESUME,
    PAUSE_ACTION_HISTORY,
    PAUSE_ACTION_RESTART,
    PAUSE_ACTION_MENU
} PauseAction;

typedef struct PauseView {
    UiButton resume;
    UiButton history;
    UiButton restart;
    UiButton menu;
    int focus;
    bool focus_visible;
} PauseView;

void pause_view_init(PauseView *view);
void pause_view_reset(PauseView *view);
PauseAction pause_view_update(PauseView *view, const UiInput *input,
                              bool accept_input, float dt);
void pause_view_draw(const PauseView *view, const UiFonts *fonts);

#endif
