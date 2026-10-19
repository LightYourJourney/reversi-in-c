#ifndef REVERSI_UI_ANIMATION_H
#define REVERSI_UI_ANIMATION_H

#include "core/board.h"

#include <stdbool.h>

#define REVERSI_UI_ANIM_MAX_FLIPS 32

typedef struct UiFlipCell {
    int row;
    int col;
    float delay;
} UiFlipCell;

typedef struct UiMoveAnimation {
    bool active;
    Stone player;
    Move move;
    float elapsed;
    float duration;
    float place_duration;
    float flip_duration;
    int flip_count;
    UiFlipCell flips[REVERSI_UI_ANIM_MAX_FLIPS];
} UiMoveAnimation;

void ui_animation_reset(UiMoveAnimation *animation);
void ui_animation_start(UiMoveAnimation *animation,
                        const Board *before, const Board *after,
                        Move move, Stone player, bool enabled);
void ui_animation_update(UiMoveAnimation *animation, float dt);
bool ui_animation_is_active(const UiMoveAnimation *animation);

/*
 * When the cell is animated, returns true and fills the stone and scale to
 * draw instead of the static board content. Pure presentation; never mutates
 * the game state.
 */
bool ui_animation_override(const UiMoveAnimation *animation,
                           int row, int col, Stone *stone,
                           float *scale_x, float *scale_y, float *alpha);

#endif
