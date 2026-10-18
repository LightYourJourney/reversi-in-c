#include "ui/menu_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct MenuLayout {
    Rectangle buttons[3];
    float mark_y;
    float title_y;
    float subtitle_y;
    float footer_y;
} MenuLayout;

static MenuLayout menu_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(400.0f, width - 64.0f);
    const float button_height = 52.0f;
    const float gap = 12.0f;
    const float x = (width - panel_width) * 0.5f;
    const float top = fmaxf(height * 0.50f, 286.0f);

    MenuLayout layout = {0};
    for (int i = 0; i < 3; ++i) {
        layout.buttons[i] = (Rectangle){
            .x = x,
            .y = top + (float)i * (button_height + gap),
            .width = panel_width,
            .height = button_height
        };
    }
    layout.mark_y = top - 150.0f;
    layout.title_y = top - 112.0f;
    layout.subtitle_y = top - 38.0f;
    layout.footer_y = height - 42.0f;
    return layout;
}

void menu_view_init(MenuView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->play, "Play", true);
    ui_button_init(&view->settings, "Settings", false);
    ui_button_init(&view->quit, "Quit", false);
    view->focus = 0;
    view->focus_visible = false;
}

static MenuAction action_for_focus(int focus)
{
    if (focus == 0) {
        return MENU_ACTION_PLAY;
    }
    if (focus == 1) {
        return MENU_ACTION_SETTINGS;
    }
    return MENU_ACTION_QUIT;
}

MenuAction menu_view_update(MenuView *view, const UiInput *input,
                            bool accept_input, float dt)
{
    if (view == NULL || input == NULL) {
        return MENU_ACTION_NONE;
    }

    const MenuLayout layout = menu_layout();
    ui_button_set_bounds(&view->play, layout.buttons[0]);
    ui_button_set_bounds(&view->settings, layout.buttons[1]);
    ui_button_set_bounds(&view->quit, layout.buttons[2]);

    const bool play_clicked = ui_button_update(
        &view->play, input, accept_input, dt);
    const bool settings_clicked = ui_button_update(
        &view->settings, input, accept_input, dt);
    const bool quit_clicked = ui_button_update(
        &view->quit, input, accept_input, dt);

    if (!accept_input) {
        return MENU_ACTION_NONE;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }

    if (input->up_pressed || input->down_pressed) {
        view->focus = (view->focus
                       + (input->down_pressed ? 1 : -1) + 3) % 3;
        view->focus_visible = true;
    }

    MenuAction action = MENU_ACTION_NONE;
    if (play_clicked) {
        action = MENU_ACTION_PLAY;
    } else if (settings_clicked) {
        action = MENU_ACTION_SETTINGS;
    } else if (quit_clicked) {
        action = MENU_ACTION_QUIT;
    } else if (input->enter_pressed) {
        action = action_for_focus(view->focus);
    }

    return action;
}

void menu_view_draw(const MenuView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }

    const float width = (float)GetScreenWidth();
    const MenuLayout layout = menu_layout();
    const float center_x = width * 0.5f;

    Rectangle mark = {
        .x = center_x - 22.0f,
        .y = layout.mark_y - 22.0f,
        .width = 44.0f,
        .height = 44.0f
    };
    ui_draw_rounded_rect(mark, 13.0f, UI_COLOR_BOARD);
    ui_draw_rounded_outline(mark, 13.0f, 1.0f, UI_COLOR_BOARD_EDGE);
    ui_draw_disc((Vector2){center_x - 8.0f, layout.mark_y}, 11.0f,
                 STONE_WHITE, 1.0f, 1.0f, 1.0f);
    ui_draw_disc((Vector2){center_x + 8.0f, layout.mark_y}, 11.0f,
                 STONE_BLACK, 1.0f, 1.0f, 1.0f);

    ui_draw_text_aligned(fonts, UI_TEXT_SEMIBOLD, "REVERSI",
                         (Rectangle){0, layout.title_y, width, 72.0f},
                         UI_FONT_DISPLAY, UI_COLOR_TEXT, UI_ALIGN_CENTER);
    ui_draw_text_aligned(fonts, UI_TEXT_REGULAR,
                         "A quiet game of strategy",
                         (Rectangle){0, layout.subtitle_y, width, 24.0f},
                         UI_FONT_SMALL, UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_CENTER);

    const float accent_width = 42.0f;
    ui_draw_rounded_rect(
        (Rectangle){center_x - accent_width * 0.5f,
                    layout.subtitle_y + 30.0f, accent_width, 2.0f},
        1.0f, ui_color_alpha(UI_COLOR_ACCENT, 0.7f));

    ui_button_draw(&view->play, fonts,
                   view->focus_visible && view->focus == 0);
    ui_button_draw(&view->settings, fonts,
                   view->focus_visible && view->focus == 1);
    ui_button_draw(&view->quit, fonts,
                   view->focus_visible && view->focus == 2);

    ui_draw_text_aligned(fonts, UI_TEXT_REGULAR, "C11 / raylib 6.2",
                         (Rectangle){0, layout.footer_y, width, 20.0f},
                         UI_FONT_TINY, UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_CENTER);
}
