#ifndef REVERSI_UI_SETTINGS_VIEW_H
#define REVERSI_UI_SETTINGS_VIEW_H

#include "ui/draw.h"

typedef enum SettingsAction {
    SETTINGS_ACTION_NONE = 0,
    SETTINGS_ACTION_TOGGLE_ANIMATIONS,
    SETTINGS_ACTION_TOGGLE_HINTS,
    SETTINGS_ACTION_TOGGLE_LAST_MOVE,
    SETTINGS_ACTION_BACK
} SettingsAction;

typedef struct SettingsView {
    UiButton back;
    int focus;
    bool focus_visible;
    float toggle_t[3];
} SettingsView;

void settings_view_init(SettingsView *view);
SettingsAction settings_view_update(SettingsView *view,
                                    const UiInput *input,
                                    bool accept_input, float dt,
                                    bool animations, bool hints,
                                    bool last_move);
void settings_view_draw(const SettingsView *view, const UiFonts *fonts);

#endif
