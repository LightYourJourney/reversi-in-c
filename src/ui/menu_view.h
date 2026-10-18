#ifndef REVERSI_UI_MENU_VIEW_H
#define REVERSI_UI_MENU_VIEW_H

#include "ui/draw.h"

typedef enum MenuAction {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_PLAY,
    MENU_ACTION_SETTINGS,
    MENU_ACTION_QUIT
} MenuAction;

typedef struct MenuView {
    UiButton play;
    UiButton settings;
    UiButton quit;
    int focus;
    bool focus_visible;
} MenuView;

void menu_view_init(MenuView *view);
MenuAction menu_view_update(MenuView *view, const UiInput *input,
                            bool accept_input, float dt);
void menu_view_draw(const MenuView *view, const UiFonts *fonts);

#endif
