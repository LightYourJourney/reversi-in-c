#include "ui/board_view.h"

#include "core/rules.h"
#include "ui/draw.h"
#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

static Rectangle cell_rect(Rectangle board, float cell, int row, int col)
{
    return (Rectangle){
        .x = board.x + (float)col * cell,
        .y = board.y + (float)row * cell,
        .width = cell,
        .height = cell
    };
}

static Vector2 cell_center(Rectangle board, float cell, int row, int col)
{
    return (Vector2){
        .x = board.x + ((float)col + 0.5f) * cell,
        .y = board.y + ((float)row + 0.5f) * cell
    };
}

static bool cell_is_legal(const GameState *game, int row, int col)
{
    if (game == NULL || game->result != GAME_RESULT_ONGOING) {
        return false;
    }
    if (!board_in_bounds(row, col)
        || board_get(&game->board, row, col) != STONE_EMPTY) {
        return false;
    }
    return rules_is_legal_move(&game->board, game->current_player,
                               (Move){.row = row, .col = col});
}

bool board_view_hit_test(Rectangle board, float cell, Vector2 point,
                         int *row, int *col)
{
    if (cell <= 0.0f
        || point.x < board.x || point.x >= board.x + board.width
        || point.y < board.y || point.y >= board.y + board.height) {
        return false;
    }

    int hit_col = (int)((point.x - board.x) / cell);
    int hit_row = (int)((point.y - board.y) / cell);
    if (hit_col < 0) {
        hit_col = 0;
    } else if (hit_col >= REVERSI_BOARD_SIZE) {
        hit_col = REVERSI_BOARD_SIZE - 1;
    }
    if (hit_row < 0) {
        hit_row = 0;
    } else if (hit_row >= REVERSI_BOARD_SIZE) {
        hit_row = REVERSI_BOARD_SIZE - 1;
    }

    if (row != NULL) {
        *row = hit_row;
    }
    if (col != NULL) {
        *col = hit_col;
    }
    return true;
}

static void draw_coordinates(Rectangle board, float cell,
                             const UiFonts *fonts)
{
    const float label_height = 18.0f;

    for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
        char label[2] = {(char)('a' + col), '\0'};
        Rectangle box = {
            .x = board.x + (float)col * cell,
            .y = board.y + board.height + 4.0f,
            .width = cell,
            .height = label_height
        };
        ui_draw_text_aligned(fonts, UI_TEXT_REGULAR, label, box,
                             UI_FONT_TINY, UI_COLOR_TEXT_FAINT,
                             UI_ALIGN_CENTER);
    }

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        char label[2] = {(char)('8' - row), '\0'};
        Rectangle box = {
            .x = board.x - 28.0f,
            .y = board.y + (float)row * cell,
            .width = 20.0f,
            .height = cell
        };
        ui_draw_text_aligned(fonts, UI_TEXT_REGULAR, label, box,
                             UI_FONT_TINY, UI_COLOR_TEXT_FAINT,
                             UI_ALIGN_RIGHT);
    }
}

void board_view_draw(const GameState *game, Rectangle board, float cell,
                     const UiFonts *fonts,
                     const UiMoveAnimation *animation,
                     bool show_legal_moves, bool show_last_move,
                     Move last_move, bool has_last_move, float last_move_age,
                     int cursor_row, int cursor_col, bool cursor_visible,
                     bool input_enabled)
{
    if (game == NULL || fonts == NULL || cell <= 0.0f) {
        return;
    }

    ui_draw_shadow(board, UI_RADIUS_MD, 12.0f, 0.45f);
    DrawRectangleRounded(board, 0.04f, 20, UI_COLOR_BOARD);
    ui_draw_rounded_outline(board, UI_RADIUS_MD, 1.5f, UI_COLOR_BOARD_EDGE);

    for (int i = 1; i < REVERSI_BOARD_SIZE; ++i) {
        const float x = board.x + (float)i * cell;
        const float y = board.y + (float)i * cell;
        DrawLineEx((Vector2){x, board.y + 2.0f},
                   (Vector2){x, board.y + board.height - 2.0f},
                   1.0f, UI_COLOR_GRID);
        DrawLineEx((Vector2){board.x + 2.0f, y},
                   (Vector2){board.x + board.width - 2.0f, y},
                   1.0f, UI_COLOR_GRID);
    }

    const bool last_visible = show_last_move && has_last_move
        && board_in_bounds(last_move.row, last_move.col);
    if (last_visible) {
        Rectangle highlight = cell_rect(board, cell,
                                        last_move.row, last_move.col);
        highlight.x += 2.0f;
        highlight.y += 2.0f;
        highlight.width -= 4.0f;
        highlight.height -= 4.0f;
        ui_draw_rounded_rect(highlight, 8.0f,
                             ui_color_alpha(UI_COLOR_ACCENT, 0.16f));
    }

    if (show_legal_moves && game->result == GAME_RESULT_ONGOING) {
        const float dot_radius = fmaxf(2.5f, cell * 0.07f);
        for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
            for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
                if (cell_is_legal(game, row, col)) {
                    const Vector2 center = cell_center(board, cell, row, col);
                    DrawCircleV(center, dot_radius,
                                ui_color_alpha(UI_COLOR_ACCENT, 0.62f));
                }
            }
        }
    }

    const float disc_radius = cell * 0.38f;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            Stone stone = board_get(&game->board, row, col);
            if (stone == STONE_EMPTY) {
                continue;
            }

            float scale_x = 1.0f;
            float scale_y = 1.0f;
            float alpha = 1.0f;
            const bool animated = ui_animation_override(
                animation, row, col, &stone, &scale_x, &scale_y, &alpha);

            if (animated || stone != STONE_EMPTY) {
                const Vector2 center = cell_center(board, cell, row, col);
                ui_draw_disc(center, disc_radius, stone, alpha,
                             scale_x, scale_y);
            }
        }
    }

    if (last_visible) {
        const Vector2 center = cell_center(board, cell,
                                           last_move.row, last_move.col);
        const float pulse = 0.58f + 0.20f
            * sinf(last_move_age * 5.0f);
        ui_draw_ring(center, cell * 0.44f, fmaxf(2.0f, cell * 0.035f),
                     ui_color_alpha(UI_COLOR_ACCENT, ui_clamp01(pulse)));
    }

    if (cursor_visible
        && board_in_bounds(cursor_row, cursor_col)
        && game->result == GAME_RESULT_ONGOING) {
        const bool legal = cell_is_legal(game, cursor_row, cursor_col);
        const Vector2 center = cell_center(board, cell,
                                           cursor_row, cursor_col);

        if (legal && input_enabled) {
            ui_draw_disc(center, disc_radius, game->current_player,
                         0.20f, 1.0f, 1.0f);
        }

        Rectangle outline = cell_rect(board, cell, cursor_row, cursor_col);
        outline.x += 3.0f;
        outline.y += 3.0f;
        outline.width -= 6.0f;
        outline.height -= 6.0f;
        ui_draw_rounded_outline(
            outline, 7.0f, 2.0f,
            ui_color_alpha(legal ? UI_COLOR_ACCENT : UI_COLOR_TEXT,
                           legal ? 0.75f : 0.28f));
    }

    draw_coordinates(board, cell, fonts);
}
