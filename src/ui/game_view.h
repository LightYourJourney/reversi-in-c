#ifndef REVERSI_UI_GAME_VIEW_H
#define REVERSI_UI_GAME_VIEW_H

#include "core/game.h"
#include "ui/animation.h"
#include "ui/draw.h"

typedef enum GameViewActionType {
    GAME_VIEW_NONE = 0,
    GAME_VIEW_MOVE,
    GAME_VIEW_RESTART,
    GAME_VIEW_MENU
} GameViewActionType;

typedef struct GameViewAction {
    GameViewActionType type;
    Move move;
} GameViewAction;

typedef struct GameView {
    UiButton restart;
    UiButton menu;
    int cursor_row;
    int cursor_col;
    bool cursor_visible;
    bool keyboard_mode;
    bool has_last_mouse;
    Vector2 last_mouse;
    float reject_timer;
    Move reject_move;
} GameView;

void game_view_init(GameView *view);
void game_view_reset(GameView *view);

GameViewAction game_view_update(GameView *view, const GameState *game,
                                const UiInput *input,
                                bool accept_input, bool board_accept,
                                float dt);

void game_view_draw(const GameView *view, const GameState *game,
                    const UiMoveAnimation *animation,
                    const UiFonts *fonts,
                    bool show_legal_moves, bool show_last_move,
                    Move last_move, bool has_last_move,
                    float last_move_age);

#endif
