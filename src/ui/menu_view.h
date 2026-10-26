#ifndef REVERSI_UI_MENU_VIEW_H
#define REVERSI_UI_MENU_VIEW_H

#include "ui/draw.h"

#define REVERSI_MENU_BUTTON_COUNT 9

typedef enum MenuAction {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_CONTINUE,
    MENU_ACTION_PLAY,
    MENU_ACTION_LAB,
    MENU_ACTION_ONLINE,
    MENU_ACTION_LOAD,
    MENU_ACTION_HELP,
    MENU_ACTION_STATS,
    MENU_ACTION_SETTINGS,
    MENU_ACTION_QUIT
} MenuAction;

typedef struct MenuView {
    UiButton buttons[REVERSI_MENU_BUTTON_COUNT];
    int focus;
    bool focus_visible;
} MenuView;

void menu_view_init(MenuView *view);
MenuAction menu_view_update(MenuView *view, const UiInput *input,
                            bool accept_input, float dt, bool has_continue);
void menu_view_draw(const MenuView *view, const UiFonts *fonts);

#endif
