#include "ui/menu_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

enum {
    MENU_INDEX_CONTINUE = 0,
    MENU_INDEX_PLAY,
    MENU_INDEX_LAB,
    MENU_INDEX_ONLINE,
    MENU_INDEX_LOAD,
    MENU_INDEX_HELP,
    MENU_INDEX_STATS,
    MENU_INDEX_SETTINGS,
    MENU_INDEX_QUIT
};

typedef struct MenuEntry {
    const char *label;
    MenuAction action;
} MenuEntry;

static const MenuEntry MENU_ENTRIES[REVERSI_MENU_BUTTON_COUNT] = {
    {"Continue", MENU_ACTION_CONTINUE},
    {"New game", MENU_ACTION_PLAY},
    {"Analysis lab", MENU_ACTION_LAB},
    {"Online play", MENU_ACTION_ONLINE},
    {"Load game", MENU_ACTION_LOAD},
    {"How to play", MENU_ACTION_HELP},
    {"Statistics", MENU_ACTION_STATS},
    {"Settings", MENU_ACTION_SETTINGS},
    {"Quit", MENU_ACTION_QUIT}
};

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
    if (index < 0 || index >= REVERSI_MENU_BUTTON_COUNT) {
        return MENU_ACTION_NONE;
    }
    return MENU_ENTRIES[index].action;
}

static unsigned disabled_mask(bool has_continue)
{
    return has_continue ? 0u : (1u << MENU_INDEX_CONTINUE);
}

void menu_view_init(MenuView *view)
{
    if (view == NULL) {
        return;
    }

    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        ui_button_init(&view->buttons[index], MENU_ENTRIES[index].label,
                       index == MENU_INDEX_CONTINUE);
    }
    view->focus = MENU_INDEX_PLAY;
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
    view->buttons[MENU_INDEX_CONTINUE].enabled = has_continue;
    view->buttons[MENU_INDEX_CONTINUE].primary = has_continue;
    view->buttons[MENU_INDEX_PLAY].primary = !has_continue;
    for (int index = 0; index < REVERSI_MENU_BUTTON_COUNT; ++index) {
        ui_button_set_bounds(&view->buttons[index], layout.buttons[index]);
        if (index != MENU_INDEX_CONTINUE) {
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
