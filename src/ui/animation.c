#include "ui/animation.h"

#include "ui/draw.h"
#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

void ui_animation_reset(UiMoveAnimation *animation)
{
    if (animation == NULL) {
        return;
    }

    animation->active = false;
    animation->player = STONE_EMPTY;
    animation->move = (Move){0, 0};
    animation->elapsed = 0.0f;
    animation->duration = 0.0f;
    animation->place_duration = REVERSI_UI_DUR_PLACE;
    animation->flip_duration = REVERSI_UI_DUR_FLIP;
    animation->flip_count = 0;
}

void ui_animation_start(UiMoveAnimation *animation,
                        const Board *before, const Board *after,
                        Move move, Stone player, bool enabled)
{
    ui_animation_reset(animation);
    if (animation == NULL || before == NULL || after == NULL || !enabled) {
        return;
    }

    animation->active = true;
    animation->player = player;
    animation->move = move;
    animation->place_duration = REVERSI_UI_DUR_PLACE;
    animation->flip_duration = REVERSI_UI_DUR_FLIP;

    const Stone opponent = stone_opponent(player);
    int count = 0;

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (row == move.row && col == move.col) {
                continue;
            }
            if (board_get(before, row, col) == opponent
                && board_get(after, row, col) == player) {
                if (count < REVERSI_UI_ANIM_MAX_FLIPS) {
                    animation->flips[count].row = row;
                    animation->flips[count].col = col;
                    animation->flips[count].delay = 0.0f;
                }
                ++count;
            }
        }
    }

    animation->flip_count = count > REVERSI_UI_ANIM_MAX_FLIPS
        ? REVERSI_UI_ANIM_MAX_FLIPS : count;

    float stagger = 0.0f;
    if (animation->flip_count > 1) {
        stagger = fminf(0.03f, 0.34f / (float)(animation->flip_count - 1));
    }

    animation->duration = animation->place_duration;
    for (int i = 0; i < animation->flip_count; ++i) {
        animation->flips[i].delay = 0.10f + (float)i * stagger;
        const float end = animation->flips[i].delay + animation->flip_duration;
        if (end > animation->duration) {
            animation->duration = end;
        }
    }
}

void ui_animation_update(UiMoveAnimation *animation, float dt)
{
    if (animation == NULL || !animation->active) {
        return;
    }

    animation->elapsed += dt;
    if (animation->elapsed >= animation->duration) {
        animation->elapsed = animation->duration;
        animation->active = false;
    }
}

void ui_animation_finish(UiMoveAnimation *animation)
{
    if (animation == NULL || !animation->active) {
        return;
    }

    animation->elapsed = animation->duration;
    animation->active = false;
}

bool ui_animation_is_active(const UiMoveAnimation *animation)
{
    return animation != NULL && animation->active;
}

bool ui_animation_override(const UiMoveAnimation *animation,
                           int row, int col, Stone *stone,
                           float *scale_x, float *scale_y, float *alpha)
{
    if (animation == NULL || !animation->active
        || stone == NULL || scale_x == NULL || scale_y == NULL
        || alpha == NULL) {
        return false;
    }

    if (row == animation->move.row && col == animation->move.col) {
        const float t = ui_clamp01(
            animation->elapsed / animation->place_duration);
        if (t >= 1.0f) {
            return false;
        }

        *stone = animation->player;
        float scale = ui_ease_out_back(t);
        if (scale > 1.06f) {
            scale = 1.06f;
        }
        *scale_x = scale;
        *scale_y = scale;
        *alpha = ui_clamp01(t * 2.5f);
        return true;
    }

    const Stone opponent = stone_opponent(animation->player);
    for (int i = 0; i < animation->flip_count; ++i) {
        if (animation->flips[i].row != row
            || animation->flips[i].col != col) {
            continue;
        }

        const float t = (animation->elapsed - animation->flips[i].delay)
            / animation->flip_duration;

        *scale_y = 1.0f;
        *alpha = 1.0f;

        if (t < 0.0f) {
            *stone = opponent;
            *scale_x = 1.0f;
            return true;
        }
        if (t < 0.5f) {
            *stone = opponent;
            *scale_x = 1.0f - 2.0f * t;
            return true;
        }
        if (t < 1.0f) {
            *stone = animation->player;
            *scale_x = 2.0f * t - 1.0f;
            return true;
        }
        return false;
    }

    return false;
}
