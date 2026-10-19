#ifndef REVERSI_UI_GAME_VIEW_H
#define REVERSI_UI_GAME_VIEW_H

#include "core/match.h"
#include "ui/animation.h"
#include "ui/draw.h"

typedef enum GameViewActionType {
    GAME_VIEW_NONE = 0,
    GAME_VIEW_MOVE,
    GAME_VIEW_UNDO,
    GAME_VIEW_REDO,
    GAME_VIEW_PAUSE,
    GAME_VIEW_RESTART,
    GAME_VIEW_MENU
} GameViewActionType;

typedef struct GameViewAction {
    GameViewActionType type;
    Move move;
} GameViewAction;

typedef struct GameView {
    UiButton undo;
    UiButton redo;
    UiButton pause;
    UiButton restart;
    UiButton menu;
    int cursor_row;
    int cursor_col;
    bool cursor_visible;
    bool keyboard_mode;
    bool has_last_mouse;
    Vector2 last_mouse;
    float reject_timer;
    float thinking_time;
    Move reject_move;
} GameView;

void game_view_init(GameView *view);
void game_view_reset(GameView *view);

GameViewAction game_view_update(GameView *view, const Match *match,
                                const UiInput *input,
                                bool accept_input, float dt);

void game_view_draw(const GameView *view, const Match *match,
                    const UiMoveAnimation *animation,
                    const UiFonts *fonts,
                    bool show_legal_moves, bool show_last_move,
                    float last_move_age, bool animations);

#endif
