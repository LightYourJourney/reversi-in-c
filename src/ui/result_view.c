#include "ui/result_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct ResultLayout {
    Rectangle panel;
    Rectangle again;
    Rectangle undo;
    Rectangle menu;
    Rectangle black_score;
    Rectangle white_score;
} ResultLayout;

static ResultLayout result_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(560.0f, width - 64.0f);
    const float panel_height = 500.0f;
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float button_height = 48.0f;
    const float gap = 10.0f;
    const float again_y = y + panel_height - 24.0f - button_height;

    ResultLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.again = (Rectangle){
        x + 24.0f, again_y, panel_width - 48.0f, button_height};
    layout.undo = (Rectangle){
        x + 24.0f, again_y - gap - button_height,
        panel_width - 48.0f, button_height};
    layout.menu = (Rectangle){
        x + 24.0f, layout.undo.y - gap - button_height,
        panel_width - 48.0f, button_height};
    layout.black_score = (Rectangle){
        x + 24.0f, y + 206.0f, panel_width * 0.5f - 36.0f, 86.0f};
    layout.white_score = (Rectangle){
        x + panel_width * 0.5f + 12.0f, y + 206.0f,
        panel_width * 0.5f - 36.0f, 86.0f};
    return layout;
}

void result_view_init(ResultView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->again, "Play again", true);
    ui_button_init(&view->undo, "Undo last move", false);
    ui_button_init(&view->menu, "Main menu", false);
    view->focus = 0;
    view->focus_visible = false;
    result_view_reset(view);
}

void result_view_reset(ResultView *view)
{
    if (view != NULL) {
        view->appear = 0.0f;
        view->focus = 0;
        view->focus_visible = false;
    }
}

ResultAction result_view_update(ResultView *view, const UiInput *input,
                                bool accept_input, bool animations,
                                bool can_undo, float dt)
{
    if (view == NULL || input == NULL) {
        return RESULT_ACTION_NONE;
    }

    view->appear = animations
        ? view->appear + dt : REVERSI_UI_DUR_RESULT;
    const bool ready = view->appear >= REVERSI_UI_DUR_RESULT;
    const bool buttons_accept = accept_input && ready;
    const ResultLayout layout = result_layout();
    ui_button_set_bounds(&view->again, layout.again);
    ui_button_set_bounds(&view->undo, layout.undo);
    ui_button_set_bounds(&view->menu, layout.menu);

    view->again.enabled = buttons_accept;
    view->undo.enabled = buttons_accept && can_undo;
    view->menu.enabled = buttons_accept;
    const bool again_clicked = ui_button_update(
        &view->again, input, buttons_accept, dt);
    const bool undo_clicked = ui_button_update(
        &view->undo, input, view->undo.enabled, dt);
    const bool menu_clicked = ui_button_update(
        &view->menu, input, buttons_accept, dt);

    if (!buttons_accept) {
        return RESULT_ACTION_NONE;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }
    if (input->up_pressed || input->down_pressed) {
        view->focus = ui_focus_step(
            view->focus, input->down_pressed ? 1 : -1, 3,
            can_undo ? 0u : (1u << 1));
        view->focus_visible = true;
    }

    if (again_clicked) {
        return RESULT_ACTION_PLAY_AGAIN;
    }
    if (undo_clicked) {
        return RESULT_ACTION_UNDO;
    }
    if (menu_clicked || input->escape_pressed) {
        return RESULT_ACTION_MENU;
    }
    if (input->enter_pressed) {
        if (view->focus == 0) {
            return RESULT_ACTION_PLAY_AGAIN;
        }
        if (view->focus == 1 && can_undo) {
            return RESULT_ACTION_UNDO;
        }
        if (view->focus == 2) {
            return RESULT_ACTION_MENU;
        }
    }
    return RESULT_ACTION_NONE;
}

static void draw_score(const UiFonts *fonts, Rectangle box, Stone player,
                       int score)
{
    const char *label = player == STONE_BLACK ? "BLACK" : "WHITE";
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, label,
        (Rectangle){box.x, box.y, box.width, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_CENTER);

    char text[8];
    snprintf(text, sizeof(text), "%d", score);
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, text,
        (Rectangle){box.x, box.y + 18.0f, box.width, 52.0f},
        REVERSI_UI_FONT_DISPLAY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_CENTER);
}

void result_view_draw(const ResultView *view, const UiFonts *fonts,
                      const Match *match)
{
    if (view == NULL || fonts == NULL || match == NULL) {
        return;
    }

    const ResultLayout layout = result_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    const float center_x = layout.panel.x + layout.panel.width * 0.5f;
    const float center_y = layout.panel.y + 82.0f;
    const float appear = ui_clamp01(view->appear / REVERSI_UI_DUR_RESULT);
    const float scale = 0.72f + 0.28f * ui_ease_out_back(appear);
    const GameResult result = match->state.result;

    if (result == GAME_RESULT_DRAW) {
        ui_draw_disc((Vector2){center_x - 15.0f, center_y}, 24.0f,
                     STONE_WHITE, 1.0f, scale, scale);
        ui_draw_disc((Vector2){center_x + 15.0f, center_y}, 24.0f,
                     STONE_BLACK, 1.0f, scale, scale);
    } else if (result == GAME_RESULT_BLACK_WIN
               || result == GAME_RESULT_WHITE_WIN) {
        const Stone winner = result == GAME_RESULT_BLACK_WIN
            ? STONE_BLACK : STONE_WHITE;
        ui_draw_ring((Vector2){center_x, center_y}, 32.0f, 2.0f,
                     ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.55f * appear));
        ui_draw_disc((Vector2){center_x, center_y}, 24.0f,
                     winner, 1.0f, scale, scale);
    }

    const char *title = ui_result_title(result);
    if (title == NULL) {
        title = "Game over";
    }

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, title,
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 122.0f,
                    layout.panel.width - 48.0f, 52.0f},
        REVERSI_UI_FONT_TITLE, REVERSI_UI_COLOR_TEXT, UI_ALIGN_CENTER);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "Final score",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 174.0f,
                    layout.panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_CENTER);

    draw_score(fonts, layout.black_score, STONE_BLACK,
               board_count(&match->state.board, STONE_BLACK));
    draw_score(fonts, layout.white_score, STONE_WHITE,
               board_count(&match->state.board, STONE_WHITE));

    DrawLineEx(
        (Vector2){center_x, layout.black_score.y + 8.0f},
        (Vector2){center_x, layout.black_score.y + 72.0f},
        1.0f, REVERSI_UI_COLOR_BORDER);

    ui_button_draw(&view->menu, fonts,
                   view->focus_visible && view->focus == 2);
    ui_button_draw(&view->undo, fonts,
                   view->focus_visible && view->focus == 1);
    ui_button_draw(&view->again, fonts,
                   view->focus_visible && view->focus == 0);
}
