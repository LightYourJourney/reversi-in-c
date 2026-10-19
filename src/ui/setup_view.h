#ifndef REVERSI_UI_SETUP_VIEW_H
#define REVERSI_UI_SETUP_VIEW_H

#include "core/match.h"
#include "ui/draw.h"

typedef enum SetupAction {
    SETUP_ACTION_NONE = 0,
    SETUP_ACTION_START,
    SETUP_ACTION_BACK
} SetupAction;

typedef enum SetupDifficulty {
    SETUP_DIFFICULTY_EASY = 0,
    SETUP_DIFFICULTY_MEDIUM,
    SETUP_DIFFICULTY_HARD
} SetupDifficulty;

typedef struct SetupView {
    GameMode mode;
    Stone human_color;
    SetupDifficulty difficulty;
    bool ai_available;
    int focus;
    bool focus_visible;
    UiButton local;
    UiButton computer;
    UiButton black;
    UiButton white;
    UiButton easy;
    UiButton medium;
    UiButton hard;
    UiButton start;
    UiButton back;
} SetupView;

void setup_view_init(SetupView *view);
void setup_view_set_ai_available(SetupView *view, bool available);
SetupAction setup_view_update(SetupView *view, const UiInput *input,
                              bool accept_input, float dt);
void setup_view_draw(const SetupView *view, const UiFonts *fonts);

#endif
