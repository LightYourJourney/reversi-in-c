#include "ui/menu_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct MenuLayout {
    Rectangle buttons[REVERSI_MENU_BUTTON_COUNT];
    float mark_y;
    float title_y;
    float subtitle_y;
    float footer_y;
} MenuLayout;

static MenuLayout menu_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(410.0f, width - 64.0f);
    const float button_height = 40.0f;
    const float gap = 6.0f;
    const float block_height = (float)REVERSI_MENU_BUTTON_COUNT
        * button_height + (float)(REVERSI_MENU_BUTTON_COUNT - 1) * gap;
    const float x = (width - panel_width) * 0.5f;
    const float top = fminf(fmaxf(height * 0.30f, 190.0f),
                            height - 70.0f - block_height);

    MenuLayout layout = {0};
    for (int i = 0; i < REVERSI_MENU_BUTTON_COUNT; ++i) {
        layout.buttons[i] = (Rectangle){
            .x = x,
            .y = top + (float)i * (button_height + gap),
            .width = panel_width,
            .height = button_height
        };
    }
    layout.mark_y = top - 120.0f;
    layout.title_y = top - 102.0f;
    layout.subtitle_y = top - 34.0f;
    layout.footer_y = height - 34.0f;
    return layout;
}

static MenuAction action_for_index(int index)
{
    switch (index) {
    case 0:
        return MENU_ACTION_CONTINUE;
    case 1:
        return MENU_ACTION_PLAY;
    case 2:
        return MENU_ACTION_LOAD;
    case 3:
        return MENU_ACTION_HELP;
    case 4:
        return MENU_ACTION_STATS;
    case 5:
        return MENU_ACTION_SETTINGS;
    case 6:
    default:
        return MENU_ACTION_QUIT;
    }
}

static unsigned disabled_mask(bool has_continue)
{
    return has_continue ? 0u : 1u;
}

void menu_view_init(MenuView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->buttons[0], "Continue", true);
    ui_button_init(&view->buttons[1], "New game", false);
    ui_button_init(&view->buttons[2], "Load game", false);
    ui_button_init(&view->buttons[3], "How to play", false);
    ui_button_init(&view->buttons[4], "Statistics", false);
    ui_button_init(&view->buttons[5], "Settings", false);
    ui_button_init(&view->buttons[6], "Quit", false);
    view->focus = 1;
    view->focus_visible = false;
}

MenuAction menu_view_update(MenuView *view, const UiInput *input,
                            bool accept_input, float dt, bool has_continue)
{
    if (view == NULL || input == NULL) {
        return MENU_ACTION_NONE;
    }

    const MenuLayout layout = menu_layout();
    const unsigned disabled = disabled_mask(has_continue);
    view->buttons[0].enabled = has_continue;
    view->buttons[0].primary = has_continue;
    view->buttons[1].primary = !has_continue;
    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        ui_button_set_bounds(&view->buttons[index], layout.buttons[index]);
        if (index != 0) {
            view->buttons[index].enabled = true;
        }
    }

    bool clicked[REVERSI_MENU_BUTTON_COUNT] = {false};
    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        clicked[index] = ui_button_update(
            &view->buttons[index], input, accept_input, dt);
    }

    if (!accept_input) {
        return MENU_ACTION_NONE;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }
    if (input->up_pressed || input->down_pressed) {
        view->focus = ui_focus_step(
            view->focus, input->down_pressed ? 1 : -1,
            REVERSI_MENU_BUTTON_COUNT, disabled);
        view->focus_visible = true;
    }

    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        if (clicked[index]) {
            return action_for_index(index);
        }
    }
    if (input->enter_pressed && view->buttons[view->focus].enabled) {
        return action_for_index(view->focus);
    }
    return MENU_ACTION_NONE;
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
    ui_draw_rounded_rect(mark, 13.0f, REVERSI_UI_COLOR_BOARD);
    ui_draw_rounded_outline(mark, 13.0f, 1.0f, REVERSI_UI_COLOR_BOARD_EDGE);
    ui_draw_disc((Vector2){center_x - 8.0f, layout.mark_y}, 11.0f,
                 STONE_WHITE, 1.0f, 1.0f, 1.0f);
    ui_draw_disc((Vector2){center_x + 8.0f, layout.mark_y}, 11.0f,
                 STONE_BLACK, 1.0f, 1.0f, 1.0f);

    ui_draw_text_aligned(fonts, UI_TEXT_SEMIBOLD, "REVERSI",
                         (Rectangle){0, layout.title_y, width, 72.0f},
                         REVERSI_UI_FONT_DISPLAY, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_CENTER);
    ui_draw_text_aligned(fonts, UI_TEXT_REGULAR,
                         "A quiet game of strategy",
                         (Rectangle){0, layout.subtitle_y, width, 24.0f},
                         REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_CENTER);

    const float accent_width = 42.0f;
    ui_draw_rounded_rect(
        (Rectangle){center_x - accent_width * 0.5f,
                    layout.subtitle_y + 30.0f, accent_width, 2.0f},
        1.0f, ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.7f));

    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        ui_button_draw(&view->buttons[index], fonts,
                       view->focus_visible && view->focus == index);
    }

    ui_draw_text_aligned(fonts, UI_TEXT_REGULAR, "C11 / raylib 6.2",
                         (Rectangle){0, layout.footer_y, width, 20.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_CENTER);
}
