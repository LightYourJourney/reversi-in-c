#include "ui/setup_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct SetupLayout {
    Rectangle panel;
    Rectangle mode_local;
    Rectangle mode_computer;
    Rectangle color_black;
    Rectangle color_white;
    Rectangle mode_row;
    Rectangle color_row;
    Rectangle start;
    Rectangle back;
} SetupLayout;

static SetupLayout setup_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(620.0f, width - 64.0f);
    const float panel_height = fminf(500.0f, height - 64.0f);
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float gap = 12.0f;
    const float mode_width = (panel_width - 48.0f - gap) * 0.5f;
    const float color_width = 132.0f;

    SetupLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.mode_local = (Rectangle){
        x + 24.0f, y + 132.0f, mode_width, 56.0f};
    layout.mode_computer = (Rectangle){
        x + 24.0f + mode_width + gap, y + 132.0f, mode_width, 56.0f};
    layout.mode_row = (Rectangle){
        x + 18.0f, y + 124.0f, panel_width - 36.0f, 72.0f};
    layout.color_black = (Rectangle){
        x + 24.0f, y + 230.0f, color_width, 48.0f};
    layout.color_white = (Rectangle){
        x + 24.0f + color_width + gap, y + 230.0f, color_width, 48.0f};
    layout.color_row = (Rectangle){
        x + 18.0f, y + 222.0f,
        color_width * 2.0f + gap + 12.0f, 64.0f};
    layout.back = (Rectangle){
        x + 24.0f, y + panel_height - 24.0f - 48.0f,
        panel_width - 48.0f, 48.0f};
    layout.start = (Rectangle){
        layout.back.x, layout.back.y - 10.0f - 48.0f,
        layout.back.width, 48.0f};
    return layout;
}

static bool color_available(const SetupView *view)
{
    return view->ai_available && view->mode == GAME_MODE_PVE;
}

static int step_focus(int focus, int direction, bool color_enabled)
{
    for (int step = 0; step < 4; ++step) {
        focus = (focus + direction + 4) % 4;
        if (focus != 1 || color_enabled) {
            return focus;
        }
    }
    return focus;
}

void setup_view_init(SetupView *view)
{
    if (view == NULL) {
        return;
    }

    view->mode = GAME_MODE_PVP;
    view->human_color = STONE_BLACK;
    view->ai_available = false;
    view->focus = 0;
    view->focus_visible = false;
    ui_button_init(&view->local, "Local", true);
    ui_button_init(&view->computer, "Computer", false);
    ui_button_init(&view->black, "Black", true);
    ui_button_init(&view->white, "White", false);
    ui_button_init(&view->start, "Start game", true);
    ui_button_init(&view->back, "Back", false);
}

void setup_view_set_ai_available(SetupView *view, bool available)
{
    if (view == NULL) {
        return;
    }

    view->ai_available = available;
    if (!available) {
        view->mode = GAME_MODE_PVP;
    }
}

SetupAction setup_view_update(SetupView *view, const UiInput *input,
                              bool accept_input, float dt)
{
    if (view == NULL || input == NULL) {
        return SETUP_ACTION_NONE;
    }

    const SetupLayout layout = setup_layout();
    const bool colors = color_available(view);

    ui_button_set_bounds(&view->local, layout.mode_local);
    ui_button_set_bounds(&view->computer, layout.mode_computer);
    ui_button_set_bounds(&view->black, layout.color_black);
    ui_button_set_bounds(&view->white, layout.color_white);
    ui_button_set_bounds(&view->start, layout.start);
    ui_button_set_bounds(&view->back, layout.back);

    view->local.primary = view->mode == GAME_MODE_PVP;
    view->computer.primary = view->mode == GAME_MODE_PVE;
    view->black.primary = view->human_color == STONE_BLACK;
    view->white.primary = view->human_color == STONE_WHITE;
    view->computer.enabled = view->ai_available;
    view->black.enabled = colors;
    view->white.enabled = colors;
    view->start.enabled = view->mode == GAME_MODE_PVP || view->ai_available;

    if (!colors && view->focus == 1) {
        view->focus = 2;
    }

    const bool local_clicked = ui_button_update(
        &view->local, input, accept_input, dt);
    const bool computer_clicked = ui_button_update(
        &view->computer, input, accept_input, dt);
    const bool black_clicked = ui_button_update(
        &view->black, input, accept_input, dt);
    const bool white_clicked = ui_button_update(
        &view->white, input, accept_input, dt);
    const bool start_clicked = ui_button_update(
        &view->start, input, accept_input, dt);
    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return SETUP_ACTION_NONE;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }
    if (input->up_pressed || input->down_pressed) {
        view->focus = step_focus(view->focus,
                                 input->down_pressed ? 1 : -1, colors);
        view->focus_visible = true;
    }

    if (input->left_pressed || input->right_pressed) {
        if (view->focus == 0 && view->ai_available) {
            view->mode = view->mode == GAME_MODE_PVP
                ? GAME_MODE_PVE : GAME_MODE_PVP;
        } else if (view->focus == 1 && colors) {
            view->human_color = view->human_color == STONE_BLACK
                ? STONE_WHITE : STONE_BLACK;
        }
    }

    if (local_clicked) {
        view->mode = GAME_MODE_PVP;
    } else if (computer_clicked && view->ai_available) {
        view->mode = GAME_MODE_PVE;
    } else if (black_clicked && colors) {
        view->human_color = STONE_BLACK;
    } else if (white_clicked && colors) {
        view->human_color = STONE_WHITE;
    }

    if (back_clicked || input->escape_pressed) {
        return SETUP_ACTION_BACK;
    }
    if (start_clicked) {
        return view->start.enabled ? SETUP_ACTION_START : SETUP_ACTION_NONE;
    }

    if (input->enter_pressed) {
        if (view->focus == 0 && view->ai_available) {
            view->mode = view->mode == GAME_MODE_PVP
                ? GAME_MODE_PVE : GAME_MODE_PVP;
        } else if (view->focus == 1 && colors) {
            view->human_color = view->human_color == STONE_BLACK
                ? STONE_WHITE : STONE_BLACK;
        } else if (view->focus == 2) {
            return view->start.enabled
                ? SETUP_ACTION_START : SETUP_ACTION_NONE;
        } else if (view->focus == 3) {
            return SETUP_ACTION_BACK;
        }
    }

    return SETUP_ACTION_NONE;
}

void setup_view_draw(const SetupView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }

    const SetupLayout layout = setup_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "New game",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 26.0f,
                    layout.panel.width - 48.0f, 40.0f},
        REVERSI_UI_FONT_TITLE, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "Choose how you want to play.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 68.0f,
                    layout.panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "MODE",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 108.0f,
                    layout.panel.width - 48.0f, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "PLAYER COLOR",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 206.0f,
                    layout.panel.width - 48.0f, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);

    if (view->focus_visible && view->focus == 0) {
        ui_draw_rounded_outline(layout.mode_row, REVERSI_UI_RADIUS_SM + 2.0f,
                                2.0f, REVERSI_UI_COLOR_FOCUS);
    }
    if (view->focus_visible && view->focus == 1 && color_available(view)) {
        ui_draw_rounded_outline(layout.color_row, REVERSI_UI_RADIUS_SM + 2.0f,
                                2.0f, REVERSI_UI_COLOR_FOCUS);
    }

    ui_button_draw(&view->local, fonts, false);
    ui_button_draw(&view->computer, fonts, false);
    ui_button_draw(&view->black, fonts, false);
    ui_button_draw(&view->white, fonts, false);
    ui_button_draw(&view->start, fonts,
                   view->focus_visible && view->focus == 2);
    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == 3);

    const char *note = view->ai_available
        ? "Local two-player is always available."
        : "Computer opponent arrives in Phase 4; local play is ready.";
    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR, note,
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 292.0f,
                    layout.panel.width - 48.0f, 24.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
}
