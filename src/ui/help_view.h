#ifndef REVERSI_UI_HELP_VIEW_H
#define REVERSI_UI_HELP_VIEW_H

#include "ui/draw.h"

typedef enum HelpAction {
    HELP_ACTION_NONE = 0,
    HELP_ACTION_BACK
} HelpAction;

typedef struct HelpView {
    UiButton previous;
    UiButton next;
    UiButton back;
    int page;
    int focus;
    bool focus_visible;
} HelpView;

void help_view_init(HelpView *view);
void help_view_reset(HelpView *view);
HelpAction help_view_update(HelpView *view, const UiInput *input,
                            bool accept_input, float dt);
void help_view_draw(const HelpView *view, const UiFonts *fonts);

#endif
