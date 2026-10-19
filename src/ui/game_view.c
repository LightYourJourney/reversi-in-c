#include "ui/game_view.h"

#include "ui/board_view.h"
#include "ui/theme.h"

#include <math.h>
#include <stdio.h>

typedef struct GameLayout {
    Rectangle board;
    float cell;
    Rectangle panel;
    Rectangle top_bar;
    Rectangle bottom_bar;
    bool compact;
} GameLayout;

static GameLayout game_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();

    GameLayout layout = {0};
    const bool wide = width >= 1000.0f && height >= 680.0f;

    if (wide) {
        const float margin = REVERSI_UI_SPACE_XL;
        const float panel_width = fminf(320.0f, fmaxf(260.0f,
                                                        width * 0.26f));
        const float gap = 28.0f;
        const float available_width = width - margin * 2.0f
            - panel_width - gap;
        const float available_height = height - margin * 2.0f - 28.0f;
        float size = fminf(available_width, available_height);
        float cell = floorf(size / (float)REVERSI_BOARD_SIZE);
        if (cell < 32.0f) {
            cell = 32.0f;
        }
        size = cell * (float)REVERSI_BOARD_SIZE;

        layout.board = (Rectangle){
            .x = margin + (available_width - size) * 0.5f,
            .y = margin + (available_height - size) * 0.5f,
            .width = size,
            .height = size
        };
        layout.cell = cell;
        layout.panel = (Rectangle){
            .x = width - margin - panel_width,
            .y = layout.board.y,
            .width = panel_width,
            .height = layout.board.height
        };
        layout.compact = false;
        return layout;
    }

    const float margin = REVERSI_UI_SPACE_LG;
    const float top_height = 74.0f;
    const float bottom_height = 74.0f;
    const float area_top = margin + top_height + 12.0f;
    const float area_bottom = height - margin - bottom_height - 12.0f;
    const float available_width = width - margin * 2.0f;
    const float available_height = area_bottom - area_top;
    float size = fminf(available_width, available_height - 24.0f);
    float cell = floorf(size / (float)REVERSI_BOARD_SIZE);
    if (cell < 28.0f) {
        cell = 28.0f;
    }
    size = cell * (float)REVERSI_BOARD_SIZE;

    layout.board = (Rectangle){
        .x = (width - size) * 0.5f,
        .y = area_top + (available_height - 24.0f - size) * 0.5f,
        .width = size,
        .height = size
    };
    layout.cell = cell;
    layout.top_bar = (Rectangle){
        .x = margin,
        .y = margin,
        .width = width - margin * 2.0f,
        .height = top_height
    };
    layout.bottom_bar = (Rectangle){
        .x = margin,
        .y = height - margin - bottom_height,
        .width = width - margin * 2.0f,
        .height = bottom_height
    };
    layout.compact = true;
    return layout;
}

void game_view_init(GameView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->restart, "Restart", false);
    ui_button_init(&view->menu, "Main menu", false);
    game_view_reset(view);
}

void game_view_reset(GameView *view)
{
    if (view == NULL) {
        return;
    }

    view->cursor_row = 3;
    view->cursor_col = 3;
    view->cursor_visible = false;
    view->keyboard_mode = false;
    view->has_last_mouse = false;
    view->last_mouse = (Vector2){0, 0};
    view->reject_timer = 0.0f;
    view->reject_move = (Move){0, 0};
}

GameViewAction game_view_update(GameView *view, const GameState *game,
                                const UiInput *input,
                                bool accept_input, bool board_accept,
                                float dt)
{
    GameViewAction action = {.type = GAME_VIEW_NONE, .move = {0, 0}};
    if (view == NULL || game == NULL || input == NULL) {
        return action;
    }

    const GameLayout layout = game_layout();

    if (layout.compact) {
        const float pad = 16.0f;
        const float gap = 10.0f;
        const float button_height = 42.0f;
        const float button_width = 132.0f;
        const Rectangle bar = layout.bottom_bar;
        const Rectangle menu = {
            .x = bar.x + bar.width - pad - button_width,
            .y = bar.y + (bar.height - button_height) * 0.5f,
            .width = button_width,
            .height = button_height
        };
        const Rectangle restart = {
            .x = menu.x - gap - button_width,
            .y = menu.y,
            .width = button_width,
            .height = button_height
        };
        ui_button_set_bounds(&view->restart, restart);
        ui_button_set_bounds(&view->menu, menu);
    } else {
        const float pad = 24.0f;
        const float gap = 10.0f;
        const float button_height = 46.0f;
        const float width = layout.panel.width - pad * 2.0f;
        const Rectangle menu = {
            .x = layout.panel.x + pad,
            .y = layout.panel.y + layout.panel.height - pad - button_height,
            .width = width,
            .height = button_height
        };
        const Rectangle restart = {
            .x = menu.x,
            .y = menu.y - gap - button_height,
            .width = width,
            .height = button_height
        };
        ui_button_set_bounds(&view->restart, restart);
        ui_button_set_bounds(&view->menu, menu);
    }

    view->restart.enabled = accept_input;
    view->menu.enabled = accept_input;

    const bool restart_clicked = ui_button_update(
        &view->restart, input, accept_input, dt);
    const bool menu_clicked = ui_button_update(
        &view->menu, input, accept_input, dt);

    if (view->reject_timer > 0.0f) {
        view->reject_timer -= dt;
        if (view->reject_timer < 0.0f) {
            view->reject_timer = 0.0f;
        }
    }

    if (accept_input) {
        if (restart_clicked || input->restart_pressed) {
            action.type = GAME_VIEW_RESTART;
        } else if (menu_clicked || input->menu_pressed
                   || input->escape_pressed) {
            action.type = GAME_VIEW_MENU;
        }
    }

    int hover_row = 0;
    int hover_col = 0;
    const bool over_board = board_view_hit_test(
        layout.board, layout.cell, input->mouse, &hover_row, &hover_col);

    const float mouse_dx = input->mouse.x - view->last_mouse.x;
    const float mouse_dy = input->mouse.y - view->last_mouse.y;
    const bool mouse_moved = !view->has_last_mouse
        || fabsf(mouse_dx) > 0.5f || fabsf(mouse_dy) > 0.5f;
    view->last_mouse = input->mouse;
    view->has_last_mouse = true;

    if (mouse_moved) {
        view->keyboard_mode = false;
    }

    if (over_board && !view->keyboard_mode) {
        view->cursor_row = hover_row;
        view->cursor_col = hover_col;
        view->cursor_visible = true;
    } else if (!view->keyboard_mode) {
        view->cursor_visible = false;
    }

    if (action.type != GAME_VIEW_NONE || !board_accept) {
        return action;
    }

    bool cursor_moved = false;
    if (input->up_pressed && view->cursor_row > 0) {
        --view->cursor_row;
        cursor_moved = true;
    }
    if (input->down_pressed
        && view->cursor_row < REVERSI_BOARD_SIZE - 1) {
        ++view->cursor_row;
        cursor_moved = true;
    }
    if (input->left_pressed && view->cursor_col > 0) {
        --view->cursor_col;
        cursor_moved = true;
    }
    if (input->right_pressed
        && view->cursor_col < REVERSI_BOARD_SIZE - 1) {
        ++view->cursor_col;
        cursor_moved = true;
    }
    if (cursor_moved) {
        view->keyboard_mode = true;
        view->cursor_visible = true;
    }

    if (over_board && input->mouse_pressed) {
        view->keyboard_mode = false;
        view->cursor_row = hover_row;
        view->cursor_col = hover_col;
        view->cursor_visible = true;

        if (board_view_cell_is_legal(game, hover_row, hover_col)) {
            action.type = GAME_VIEW_MOVE;
            action.move = (Move){.row = hover_row, .col = hover_col};
        } else {
            view->reject_timer = REVERSI_UI_DUR_REJECT;
            view->reject_move = (Move){.row = hover_row, .col = hover_col};
        }
    } else if (input->enter_pressed && view->cursor_visible) {
        if (board_view_cell_is_legal(game, view->cursor_row,
                                     view->cursor_col)) {
            action.type = GAME_VIEW_MOVE;
            action.move = (Move){.row = view->cursor_row,
                                 .col = view->cursor_col};
        } else {
            view->reject_timer = REVERSI_UI_DUR_REJECT;
            view->reject_move = (Move){.row = view->cursor_row,
                                       .col = view->cursor_col};
        }
    }

    return action;
}

static void draw_turn_card(const UiFonts *fonts, Rectangle card,
                           Stone player)
{
    ui_draw_rounded_rect(card, REVERSI_UI_RADIUS_SM, REVERSI_UI_COLOR_ACCENT_SOFT);
    ui_draw_rounded_outline(card, REVERSI_UI_RADIUS_SM, 1.0f,
                            ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.24f));
    ui_draw_disc((Vector2){card.x + 30.0f, card.y + card.height * 0.5f},
                 14.0f, player, 1.0f, 1.0f, 1.0f);

    char text[48];
    snprintf(text, sizeof(text), "%s to move",
             player == STONE_BLACK ? "Black" : "White");
    ui_draw_text_aligned(fonts, UI_TEXT_SEMIBOLD, text,
                         (Rectangle){card.x + 54.0f, card.y,
                                     card.width - 68.0f, card.height},
                         REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
}

static void draw_player_row(const UiFonts *fonts, Rectangle row,
                            Stone player, int score, bool active)
{
    if (active) {
        ui_draw_rounded_rect(row, REVERSI_UI_RADIUS_SM, REVERSI_UI_COLOR_SURFACE_ALT);
        ui_draw_rounded_rect(
            (Rectangle){row.x + 1.0f, row.y + 9.0f, 3.0f,
                        row.height - 18.0f},
            1.5f, REVERSI_UI_COLOR_ACCENT);
    }

    ui_draw_disc((Vector2){row.x + 28.0f, row.y + row.height * 0.5f},
                 13.0f, player, 1.0f, 1.0f, 1.0f);

    const char *label = player == STONE_BLACK ? "Black" : "White";
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, label,
        (Rectangle){row.x + 50.0f, row.y, row.width - 130.0f, row.height},
        REVERSI_UI_FONT_BODY,
        active ? REVERSI_UI_COLOR_TEXT : REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    char score_text[8];
    snprintf(score_text, sizeof(score_text), "%d", score);
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, score_text,
        (Rectangle){row.x + row.width - 76.0f, row.y, 56.0f, row.height},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_RIGHT);
}

static void draw_score_chip(const UiFonts *fonts, Rectangle box,
                            Stone player, int score, bool active)
{
    ui_draw_disc((Vector2){box.x + 12.0f, box.y + box.height * 0.5f},
                 10.0f, player, 1.0f, 1.0f, 1.0f);
    char text[8];
    snprintf(text, sizeof(text), "%d", score);
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, text,
        (Rectangle){box.x + 28.0f, box.y, box.width - 28.0f, box.height},
        REVERSI_UI_FONT_HEADING,
        active ? REVERSI_UI_COLOR_TEXT : REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
}

static void draw_compact_bar(const UiFonts *fonts, const GameLayout *layout,
                             const GameState *game, int black, int white,
                             const char *status, const char *last_move)
{
    const Rectangle top = layout->top_bar;
    ui_draw_panel(top, REVERSI_UI_RADIUS_MD, REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_disc((Vector2){top.x + 32.0f, top.y + top.height * 0.5f},
                 15.0f, game->current_player, 1.0f, 1.0f, 1.0f);
    char turn[48];
    snprintf(turn, sizeof(turn), "%s to move",
             game->current_player == STONE_BLACK ? "Black" : "White");
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, turn,
        (Rectangle){top.x + 58.0f, top.y, top.width * 0.45f, top.height},
        REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);

    const Rectangle black_chip = {
        .x = top.x + top.width - 210.0f,
        .y = top.y,
        .width = 92.0f,
        .height = top.height
    };
    const Rectangle white_chip = {
        .x = top.x + top.width - 110.0f,
        .y = top.y,
        .width = 92.0f,
        .height = top.height
    };
    draw_score_chip(fonts, black_chip, STONE_BLACK, black,
                    game->current_player == STONE_BLACK);
    draw_score_chip(fonts, white_chip, STONE_WHITE, white,
                    game->current_player == STONE_WHITE);

    const Rectangle bottom = layout->bottom_bar;
    ui_draw_panel(bottom, REVERSI_UI_RADIUS_MD, REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    char detail[96];
    snprintf(detail, sizeof(detail), "%s  /  %s", status, last_move);
    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR, detail,
        (Rectangle){bottom.x + 18.0f, bottom.y,
                    bottom.width - 330.0f, bottom.height},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
}

static void draw_wide_panel(const UiFonts *fonts, const GameLayout *layout,
                            const GameState *game, int black, int white,
                            const char *status, const char *last_move)
{
    const Rectangle panel = layout->panel;
    ui_draw_panel(panel, REVERSI_UI_RADIUS_LG, REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "MATCH",
        (Rectangle){panel.x + 24.0f, panel.y + 18.0f,
                    panel.width - 48.0f, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);

    const Rectangle turn_card = {
        .x = panel.x + 24.0f,
        .y = panel.y + 46.0f,
        .width = panel.width - 48.0f,
        .height = 64.0f
    };
    draw_turn_card(fonts, turn_card, game->current_player);

    const Rectangle black_row = {
        .x = panel.x + 16.0f,
        .y = panel.y + 130.0f,
        .width = panel.width - 32.0f,
        .height = 58.0f
    };
    const Rectangle white_row = {
        .x = black_row.x,
        .y = black_row.y + 66.0f,
        .width = black_row.width,
        .height = black_row.height
    };
    draw_player_row(fonts, black_row, STONE_BLACK, black,
                    game->current_player == STONE_BLACK);
    draw_player_row(fonts, white_row, STONE_WHITE, white,
                    game->current_player == STONE_WHITE);

    const float divider_y = white_row.y + white_row.height + 16.0f;
    DrawLineEx((Vector2){panel.x + 24.0f, divider_y},
               (Vector2){panel.x + panel.width - 24.0f, divider_y},
               1.0f, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR, status,
        (Rectangle){panel.x + 24.0f, divider_y + 14.0f,
                    panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR, last_move,
        (Rectangle){panel.x + 24.0f, divider_y + 40.0f,
                    panel.width - 48.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
}

void game_view_draw(const GameView *view, const GameState *game,
                    const UiMoveAnimation *animation,
                    const UiFonts *fonts,
                    bool show_legal_moves, bool show_last_move,
                    Move last_move, bool has_last_move,
                    float last_move_age, bool animations)
{
    if (view == NULL || game == NULL || fonts == NULL) {
        return;
    }

    const GameLayout layout = game_layout();
    const bool board_input = !ui_animation_is_active(animation)
        && game->result == GAME_RESULT_ONGOING;

    board_view_draw(game, layout.board, layout.cell, fonts, animation,
                    show_legal_moves, show_last_move,
                    last_move, has_last_move, last_move_age,
                    view->cursor_row, view->cursor_col,
                    view->cursor_visible, board_input, animations);

    if (view->reject_timer > 0.0f
        && board_in_bounds(view->reject_move.row, view->reject_move.col)) {
        const float alpha = ui_clamp01(
            view->reject_timer / REVERSI_UI_DUR_REJECT);
        const Rectangle cell = {
            .x = layout.board.x
                + (float)view->reject_move.col * layout.cell + 3.0f,
            .y = layout.board.y
                + (float)view->reject_move.row * layout.cell + 3.0f,
            .width = layout.cell - 6.0f,
            .height = layout.cell - 6.0f
        };
        ui_draw_rounded_outline(cell, 8.0f, 2.0f,
                                ui_color_alpha(REVERSI_UI_COLOR_DANGER, alpha));
    }

    const int black = board_count(&game->board, STONE_BLACK);
    const int white = board_count(&game->board, STONE_WHITE);

    char status[64];
    if (game->result != GAME_RESULT_ONGOING) {
        snprintf(status, sizeof(status), "Game over");
    } else if (ui_animation_is_active(animation)) {
        snprintf(status, sizeof(status), "Resolving move...");
    } else if (game_must_pass(game)) {
        snprintf(status, sizeof(status), "%s must pass",
                 game->current_player == STONE_BLACK ? "Black" : "White");
    } else {
        snprintf(status, sizeof(status), "Select a highlighted cell");
    }

    char last_move_text[48];
    const char *last_text = "No moves yet";
    char notation[3];
    if (has_last_move && move_to_notation(last_move, notation)) {
        snprintf(last_move_text, sizeof(last_move_text),
                 "Last move  %s", notation);
        last_text = last_move_text;
    }

    if (layout.compact) {
        draw_compact_bar(fonts, &layout, game, black, white,
                         status, last_text);
    } else {
        draw_wide_panel(fonts, &layout, game, black, white,
                        status, last_text);
    }

    ui_button_draw(&view->restart, fonts, false);
    ui_button_draw(&view->menu, fonts, false);
}
