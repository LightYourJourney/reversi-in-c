#include "ui/game_view.h"

#include "ui/board_view.h"
#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
    const float bottom_height = 84.0f;
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

static void set_button_bounds(GameView *view, const GameLayout *layout)
{
    if (layout->compact) {
        const float pad = 16.0f;
        const float gap = 6.0f;
        const float button_width = 88.0f;
        const float button_height = 42.0f;
        const Rectangle bar = layout->bottom_bar;
        const float y = bar.y + (bar.height - button_height) * 0.5f;
        float x = bar.x + bar.width - pad - button_width;

        ui_button_set_bounds(&view->menu, (Rectangle){
            x, y, button_width, button_height});
        x -= gap + button_width;
        ui_button_set_bounds(&view->restart, (Rectangle){
            x, y, button_width, button_height});
        x -= gap + button_width;
        ui_button_set_bounds(&view->pause, (Rectangle){
            x, y, button_width, button_height});
        x -= gap + button_width;
        ui_button_set_bounds(&view->redo, (Rectangle){
            x, y, button_width, button_height});
        x -= gap + button_width;
        ui_button_set_bounds(&view->undo, (Rectangle){
            x, y, button_width, button_height});
        return;
    }

    const float pad = 24.0f;
    const float gap = 10.0f;
    const float button_height = 46.0f;
    const float width = layout->panel.width - pad * 2.0f;
    const float x = layout->panel.x + pad;
    const float menu_y = layout->panel.y + layout->panel.height
        - pad - button_height;
    const float restart_y = menu_y - gap - button_height;
    const float pause_y = restart_y - gap - button_height;
    const float history_y = pause_y - gap - 42.0f;
    const float half = (width - gap) * 0.5f;

    ui_button_set_bounds(&view->menu, (Rectangle){
        x, menu_y, width, button_height});
    ui_button_set_bounds(&view->restart, (Rectangle){
        x, restart_y, width, button_height});
    ui_button_set_bounds(&view->pause, (Rectangle){
        x, pause_y, width, button_height});
    ui_button_set_bounds(&view->undo, (Rectangle){
        x, history_y, half, 42.0f});
    ui_button_set_bounds(&view->redo, (Rectangle){
        x + half + gap, history_y, half, 42.0f});
}

void game_view_init(GameView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->undo, "Undo", false);
    ui_button_init(&view->redo, "Redo", false);
    ui_button_init(&view->pause, "Pause", false);
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
    view->thinking_time = 0.0f;
    view->reject_move = (Move){0, 0};
}

GameViewAction game_view_update(GameView *view, const Match *match,
                                const UiInput *input,
                                bool accept_input, float dt)
{
    GameViewAction action = {.type = GAME_VIEW_NONE, .move = {0, 0}};
    if (view == NULL || match == NULL || input == NULL) {
        return action;
    }

    const GameLayout layout = game_layout();
    set_button_bounds(view, &layout);

    const bool player_turn = match->phase == MATCH_PHASE_PLAYER_TURN;
    const bool game_over = match->phase == MATCH_PHASE_GAME_OVER;
    const bool ai_thinking = match->phase == MATCH_PHASE_AI_THINKING;
    const bool human_turn = match_is_human_turn(match);
    const bool can_use_actions = accept_input
        && (player_turn || game_over || ai_thinking);

    view->thinking_time = ai_thinking ? view->thinking_time + dt : 0.0f;
    view->undo.enabled = can_use_actions && match_can_undo(match);
    view->redo.enabled = accept_input && (player_turn || game_over)
        && match_can_redo(match);
    view->pause.enabled = accept_input && player_turn && human_turn;
    view->restart.enabled = can_use_actions;
    view->menu.enabled = can_use_actions;

    const bool undo_clicked = ui_button_update(
        &view->undo, input, view->undo.enabled, dt);
    const bool redo_clicked = ui_button_update(
        &view->redo, input, view->redo.enabled, dt);
    const bool pause_clicked = ui_button_update(
        &view->pause, input, view->pause.enabled, dt);
    const bool restart_clicked = ui_button_update(
        &view->restart, input, view->restart.enabled, dt);
    const bool menu_clicked = ui_button_update(
        &view->menu, input, view->menu.enabled, dt);

    if (view->reject_timer > 0.0f) {
        view->reject_timer -= dt;
        if (view->reject_timer < 0.0f) {
            view->reject_timer = 0.0f;
        }
    }

    if (accept_input) {
        if (undo_clicked || (view->undo.enabled && input->undo_pressed)) {
            action.type = GAME_VIEW_UNDO;
        } else if (redo_clicked
                   || (view->redo.enabled && input->redo_pressed)) {
            action.type = GAME_VIEW_REDO;
        } else if (pause_clicked
                   || (player_turn && human_turn
                       && (input->pause_pressed
                           || input->escape_pressed))) {
            action.type = GAME_VIEW_PAUSE;
        } else if (restart_clicked || input->restart_pressed) {
            action.type = GAME_VIEW_RESTART;
        } else if (menu_clicked || input->menu_pressed) {
            action.type = GAME_VIEW_MENU;
        }
    }

    const bool board_accept = accept_input && player_turn && human_turn;
    int hover_row = 0;
    int hover_col = 0;
    const bool over_board = board_accept && board_view_hit_test(
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

    if (!board_accept) {
        view->cursor_visible = false;
        return action;
    }

    if (over_board && !view->keyboard_mode) {
        view->cursor_row = hover_row;
        view->cursor_col = hover_col;
        view->cursor_visible = true;
    } else if (!view->keyboard_mode) {
        view->cursor_visible = false;
    }

    if (action.type != GAME_VIEW_NONE) {
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

        if (board_view_cell_is_legal(&match->state, hover_row, hover_col)) {
            action.type = GAME_VIEW_MOVE;
            action.move = (Move){.row = hover_row, .col = hover_col};
        } else {
            view->reject_timer = REVERSI_UI_DUR_REJECT;
            view->reject_move = (Move){.row = hover_row, .col = hover_col};
        }
    } else if (input->enter_pressed && view->cursor_visible) {
        if (board_view_cell_is_legal(&match->state, view->cursor_row,
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

static void draw_result_discs(Vector2 center, GameResult result)
{
    if (result == GAME_RESULT_DRAW) {
        ui_draw_disc((Vector2){center.x - 8.0f, center.y}, 12.0f,
                     STONE_WHITE, 1.0f, 1.0f, 1.0f);
        ui_draw_disc((Vector2){center.x + 8.0f, center.y}, 12.0f,
                     STONE_BLACK, 1.0f, 1.0f, 1.0f);
        return;
    }
    ui_draw_disc(center, 14.0f,
                 result == GAME_RESULT_BLACK_WIN ? STONE_BLACK : STONE_WHITE,
                 1.0f, 1.0f, 1.0f);
}

static void draw_turn_card(const UiFonts *fonts, Rectangle card,
                           const Match *match)
{
    ui_draw_rounded_rect(card, REVERSI_UI_RADIUS_SM,
                         REVERSI_UI_COLOR_ACCENT_SOFT);
    ui_draw_rounded_outline(card, REVERSI_UI_RADIUS_SM, 1.0f,
                            ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.24f));

    const Vector2 center = {
        card.x + 30.0f, card.y + card.height * 0.5f
    };
    const char *title = ui_match_result_title(match);
    char text[48];
    if (title != NULL) {
        draw_result_discs(center, match->state.result);
        snprintf(text, sizeof(text), "%s", title);
    } else if (match->phase == MATCH_PHASE_AI_THINKING) {
        ui_draw_disc(center, 14.0f, match->state.current_player,
                     1.0f, 1.0f, 1.0f);
        snprintf(text, sizeof(text), "Computer thinking");
    } else {
        ui_draw_disc(center, 14.0f, match->state.current_player,
                     1.0f, 1.0f, 1.0f);
        snprintf(text, sizeof(text), "%s to move",
                 ui_player_name(match->state.current_player));
    }

    ui_draw_text_aligned(fonts, UI_TEXT_SEMIBOLD, text,
                         (Rectangle){card.x + 54.0f, card.y,
                                     card.width - 68.0f, card.height},
                         REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_LEFT);
}

static void draw_player_row(const UiFonts *fonts, Rectangle row,
                            Stone player, int score, bool active)
{
    if (active) {
        ui_draw_rounded_rect(row, REVERSI_UI_RADIUS_SM,
                             REVERSI_UI_COLOR_SURFACE_ALT);
        ui_draw_rounded_rect(
            (Rectangle){row.x + 1.0f, row.y + 9.0f, 3.0f,
                        row.height - 18.0f},
            1.5f, REVERSI_UI_COLOR_ACCENT);
    }

    ui_draw_disc((Vector2){row.x + 28.0f, row.y + row.height * 0.5f},
                 13.0f, player, 1.0f, 1.0f, 1.0f);

    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, ui_player_name(player),
        (Rectangle){row.x + 50.0f, row.y, row.width - 130.0f, row.height},
        REVERSI_UI_FONT_BODY,
        active ? REVERSI_UI_COLOR_TEXT : REVERSI_UI_COLOR_TEXT_MUTED,
        UI_ALIGN_LEFT);

    char score_text[8];
    snprintf(score_text, sizeof(score_text), "%d", score);
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, score_text,
        (Rectangle){row.x + row.width - 76.0f, row.y, 56.0f, row.height},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_RIGHT);
}

static void draw_wide_panel(const UiFonts *fonts, const GameLayout *layout,
                            const Match *match, const char *status,
                            const char *last_move)
{
    const Rectangle panel = layout->panel;
    ui_draw_panel(panel, REVERSI_UI_RADIUS_LG, REVERSI_UI_COLOR_SURFACE,
                  REVERSI_UI_COLOR_BORDER);

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
    draw_turn_card(fonts, turn_card, match);

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
    draw_player_row(fonts, black_row, STONE_BLACK,
                    board_count(&match->state.board, STONE_BLACK),
                    match->state.current_player == STONE_BLACK);
    draw_player_row(fonts, white_row, STONE_WHITE,
                    board_count(&match->state.board, STONE_WHITE),
                    match->state.current_player == STONE_WHITE);

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

static void draw_compact_bar(const UiFonts *fonts, const GameLayout *layout,
                             const Match *match, const char *status,
                             const char *last_move)
{
    const Rectangle top = layout->top_bar;
    ui_draw_panel(top, REVERSI_UI_RADIUS_MD, REVERSI_UI_COLOR_SURFACE,
                  REVERSI_UI_COLOR_BORDER);

    const Vector2 center = {
        top.x + 32.0f, top.y + top.height * 0.5f
    };
    const char *title = ui_match_result_title(match);
    char turn[48];
    if (title != NULL) {
        draw_result_discs(center, match->state.result);
        snprintf(turn, sizeof(turn), "%s", title);
    } else if (match->phase == MATCH_PHASE_AI_THINKING) {
        ui_draw_disc(center, 15.0f, match->state.current_player,
                     1.0f, 1.0f, 1.0f);
        snprintf(turn, sizeof(turn), "Computer thinking");
    } else {
        ui_draw_disc(center, 15.0f, match->state.current_player,
                     1.0f, 1.0f, 1.0f);
        snprintf(turn, sizeof(turn), "%s to move",
                 ui_player_name(match->state.current_player));
    }
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, turn,
        (Rectangle){top.x + 58.0f, top.y, top.width * 0.45f, top.height},
        REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);

    char score[32];
    const int black = board_count(&match->state.board, STONE_BLACK);
    const int white = board_count(&match->state.board, STONE_WHITE);
    snprintf(score, sizeof(score), "Black %d    White %d", black, white);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, score,
        (Rectangle){top.x + top.width - 260.0f, top.y, 240.0f,
                    top.height},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_RIGHT);

    const Rectangle bottom = layout->bottom_bar;
    ui_draw_panel(bottom, REVERSI_UI_RADIUS_MD, REVERSI_UI_COLOR_SURFACE,
                  REVERSI_UI_COLOR_BORDER);

    char detail[128];
    snprintf(detail, sizeof(detail), "%s  /  %s", status, last_move);
    const float text_width = bottom.width - 5.0f * 88.0f - 4.0f * 6.0f
        - 34.0f - 16.0f;
    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR, detail,
        (Rectangle){bottom.x + 16.0f, bottom.y, text_width, bottom.height},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
}

static void draw_pass_toast(const UiFonts *fonts, const GameLayout *layout,
                            const Match *match)
{
    const float width = 360.0f;
    const float height = 84.0f;
    const float x = layout->board.x + (layout->board.width - width) * 0.5f;
    const float y = layout->board.y + 18.0f;
    const Rectangle panel = {x, y, width, height};

    ui_draw_panel(panel, REVERSI_UI_RADIUS_MD, REVERSI_UI_COLOR_SURFACE,
                  REVERSI_UI_COLOR_BORDER_STRONG);

    char title[64];
    snprintf(title, sizeof(title), "%s has no legal moves",
             ui_player_name(match->last_pass.player));
    char body[64];
    snprintf(body, sizeof(body), "Turn passed to %s",
             ui_player_name(match->state.current_player));

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, title,
        (Rectangle){panel.x + 18.0f, panel.y + 14.0f,
                    panel.width - 36.0f, 28.0f},
        REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_CENTER);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, body,
        (Rectangle){panel.x + 18.0f, panel.y + 44.0f,
                    panel.width - 36.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_CENTER);
}

void game_view_draw(const GameView *view, const Match *match,
                    const UiMoveAnimation *animation,
                    const UiFonts *fonts,
                    bool show_legal_moves, bool show_last_move,
                    float last_move_age, bool animations)
{
    if (view == NULL || match == NULL || fonts == NULL) {
        return;
    }

    const GameLayout layout = game_layout();
    const bool board_input = match->phase == MATCH_PHASE_PLAYER_TURN;

    board_view_draw(&match->state, layout.board, layout.cell, fonts,
                    animation, show_legal_moves, show_last_move,
                    match->last_move, match->has_last_move, last_move_age,
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
                                ui_color_alpha(REVERSI_UI_COLOR_DANGER,
                                               alpha));
    }

    char status[64];
    switch (match->phase) {
    case MATCH_PHASE_GAME_OVER:
        snprintf(status, sizeof(status), "Game over");
        break;
    case MATCH_PHASE_ANIMATING:
        snprintf(status, sizeof(status), "Resolving move...");
        break;
    case MATCH_PHASE_PASSING_TURN:
        snprintf(status, sizeof(status), "%s must pass",
                 ui_player_name(match->last_pass.player));
        break;
    case MATCH_PHASE_PAUSED:
        snprintf(status, sizeof(status), "Paused");
        break;
    case MATCH_PHASE_AI_THINKING: {
        const int dots = ((int)(view->thinking_time * 3.0f)) % 4;
        snprintf(status, sizeof(status), "Computer is thinking%.*s",
                 dots, "...");
        break;
    }
    case MATCH_PHASE_PLAYER_TURN:
    default:
        if (game_must_pass(&match->state)) {
            snprintf(status, sizeof(status), "%s must pass",
                     ui_player_name(match->state.current_player));
        } else {
            snprintf(status, sizeof(status), "Select a highlighted cell");
        }
        break;
    }

    char last_move_text[64] = "No moves yet";
    char notation[3];
    if (match->has_last_move && move_to_notation(match->last_move, notation)) {
        snprintf(last_move_text, sizeof(last_move_text),
                 "Last move  %s", notation);
        if (match->last_pass.occurred) {
            snprintf(last_move_text + strlen(last_move_text),
                     sizeof(last_move_text) - strlen(last_move_text),
                     "  /  %s passed",
                     ui_player_name(match->last_pass.player));
        }
    }

    if (layout.compact) {
        draw_compact_bar(fonts, &layout, match, status, last_move_text);
    } else {
        draw_wide_panel(fonts, &layout, match, status, last_move_text);
    }

    if (match->phase == MATCH_PHASE_PASSING_TURN) {
        draw_pass_toast(fonts, &layout, match);
    }

    ui_button_draw(&view->undo, fonts, false);
    ui_button_draw(&view->redo, fonts, false);
    ui_button_draw(&view->pause, fonts, false);
    ui_button_draw(&view->restart, fonts, false);
    ui_button_draw(&view->menu, fonts, false);
}
