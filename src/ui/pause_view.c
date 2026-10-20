#include "ui/pause_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct PauseLayout {
    Rectangle panel;
    Rectangle resume;
    Rectangle save;
    Rectangle history;
    Rectangle restart;
    Rectangle menu;
} PauseLayout;

static PauseLayout pause_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(400.0f, width - 64.0f);
    const float panel_height = 430.0f;
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float button_height = 46.0f;
    const float gap = 10.0f;
    const float menu_y = y + panel_height - 24.0f - button_height;
    const float restart_y = menu_y - gap - button_height;
    const float history_y = restart_y - gap - button_height;
    const float save_y = history_y - gap - button_height;
    const float resume_y = save_y - gap - button_height;

    PauseLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.resume = (Rectangle){
        x + 24.0f, resume_y, panel_width - 48.0f, button_height};
    layout.save = (Rectangle){
        x + 24.0f, save_y, panel_width - 48.0f, button_height};
    layout.history = (Rectangle){
        x + 24.0f, history_y, panel_width - 48.0f, button_height};
    layout.restart = (Rectangle){
        x + 24.0f, restart_y, panel_width - 48.0f, button_height};
    layout.menu = (Rectangle){
        x + 24.0f, menu_y, panel_width - 48.0f, button_height};
    return layout;
}

void pause_view_init(PauseView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->resume, "Resume", true);
    ui_button_init(&view->save, "Save game", false);
    ui_button_init(&view->history, "History", false);
    ui_button_init(&view->restart, "Restart", false);
    ui_button_init(&view->menu, "Main menu", false);
    pause_view_reset(view);
}

void pause_view_reset(PauseView *view)
{
    if (view != NULL) {
        view->focus = 0;
        view->focus_visible = false;
    }
}

PauseAction pause_view_update(PauseView *view, const UiInput *input,
                              bool accept_input, float dt)
{
    if (view == NULL || input == NULL) {
        return PAUSE_ACTION_NONE;
    }

    const PauseLayout layout = pause_layout();
    ui_button_set_bounds(&view->resume, layout.resume);
    ui_button_set_bounds(&view->save, layout.save);
    ui_button_set_bounds(&view->history, layout.history);
    ui_button_set_bounds(&view->restart, layout.restart);
    ui_button_set_bounds(&view->menu, layout.menu);

    const bool resume_clicked = ui_button_update(
        &view->resume, input, accept_input, dt);
    const bool save_clicked = ui_button_update(
        &view->save, input, accept_input, dt);
    const bool history_clicked = ui_button_update(
        &view->history, input, accept_input, dt);
    const bool restart_clicked = ui_button_update(
        &view->restart, input, accept_input, dt);
    const bool menu_clicked = ui_button_update(
        &view->menu, input, accept_input, dt);

    if (!accept_input) {
        return PAUSE_ACTION_NONE;
    }

    ui_focus_update(&view->focus, &view->focus_visible, 5, input);

    if (resume_clicked || input->escape_pressed || input->pause_pressed) {
        return PAUSE_ACTION_RESUME;
    }
    if (save_clicked) {
        return PAUSE_ACTION_SAVE;
    }
    if (history_clicked || input->history_pressed) {
        return PAUSE_ACTION_HISTORY;
    }
    if (restart_clicked) {
        return PAUSE_ACTION_RESTART;
    }
    if (menu_clicked) {
        return PAUSE_ACTION_MENU;
    }
    if (input->enter_pressed) {
        switch (view->focus) {
        case 0:
            return PAUSE_ACTION_RESUME;
        case 1:
            return PAUSE_ACTION_SAVE;
        case 2:
            return PAUSE_ACTION_HISTORY;
        case 3:
            return PAUSE_ACTION_RESTART;
        case 4:
            return PAUSE_ACTION_MENU;
        default:
            break;
        }
    }

    return PAUSE_ACTION_NONE;
}

void pause_view_draw(const PauseView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }

    const PauseLayout layout = pause_layout();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  ui_color_alpha(REVERSI_UI_COLOR_OVERLAY, 0.72f));
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER_STRONG);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "Paused",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 24.0f,
                    layout.panel.width - 48.0f, 38.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "The game is waiting.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 64.0f,
                    layout.panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    ui_button_draw(&view->resume, fonts,
                   view->focus_visible && view->focus == 0);
    ui_button_draw(&view->save, fonts,
                   view->focus_visible && view->focus == 1);
    ui_button_draw(&view->history, fonts,
                   view->focus_visible && view->focus == 2);
    ui_button_draw(&view->restart, fonts,
                   view->focus_visible && view->focus == 3);
    ui_button_draw(&view->menu, fonts,
                   view->focus_visible && view->focus == 4);
}
