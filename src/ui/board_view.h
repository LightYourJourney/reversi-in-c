#ifndef REVERSI_UI_BOARD_VIEW_H
#define REVERSI_UI_BOARD_VIEW_H

#include "core/game.h"
#include "ui/animation.h"
#include "ui/fonts.h"

#include <raylib.h>
#include <stdbool.h>

bool board_view_hit_test(Rectangle board, float cell, Vector2 point,
                         int *row, int *col);

void board_view_draw(const GameState *game, Rectangle board, float cell,
                     const UiFonts *fonts,
                     const UiMoveAnimation *animation,
                     bool show_legal_moves, bool show_last_move,
                     Move last_move, bool has_last_move, float last_move_age,
                     int cursor_row, int cursor_col, bool cursor_visible,
                     bool input_enabled);

#endif
