#include "ui/help_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define HELP_PAGE_COUNT 3
#define HELP_FOCUS_PREVIOUS 0
#define HELP_FOCUS_NEXT 1
#define HELP_FOCUS_BACK 2

typedef struct HelpLayout {
    Rectangle panel;
    Rectangle previous;
    Rectangle next;
    Rectangle back;
    Rectangle body;
} HelpLayout;

typedef struct HelpPage {
    const char *title;
    const char *lines[10];
} HelpPage;

static const HelpPage HELP_PAGES[HELP_PAGE_COUNT] = {
    {
        "The goal",
        {
            "Place a disc so one or more of your opponent's discs are",
            "trapped between the new disc and another of your colour.",
            "Every trapped disc flips to your colour.",
            "",
            "A move is legal only when it flips at least one disc.",
            "Legal cells are highlighted when hints are enabled.",
            NULL
        }
    },
    {
        "Turns and passing",
        {
            "Black moves first. If you have no legal move, your turn is",
            "skipped automatically; if neither side can move, the game",
            "ends.",
            "",
            "The player with more discs wins. Equal counts are a draw.",
            "Against the computer, Undo returns to your previous",
            "decision point, so the computer's reply is undone too.",
            NULL
        }
    },
    {
        "Controls",
        {
            "Mouse: click a highlighted cell to place a disc.",
            "Keyboard: arrow keys move the board cursor; Enter places.",
            "",
            "Esc or P pauses. H opens history. U or Ctrl+Z undoes;",
            "Y or Ctrl+Shift+Z redoes. R restarts and M returns to the",
            "menu. Ctrl/Cmd+S saves an ongoing game.",
            "",
            "Settings can turn off animations and change the theme.",
            NULL
        }
    }
};

static HelpLayout help_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(620.0f, width - 64.0f);
    const float panel_height = fminf(520.0f, height - 64.0f);
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float button_height = 44.0f;
    const float button_y = y + panel_height - 24.0f - button_height;

    HelpLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.back = (Rectangle){x + 24.0f, button_y, 140.0f, button_height};
    layout.previous = (Rectangle){
        x + panel_width - 24.0f - 212.0f, button_y, 100.0f, button_height};
    layout.next = (Rectangle){
        x + panel_width - 24.0f - 100.0f, button_y, 100.0f, button_height};
    layout.body = (Rectangle){
        x + 32.0f, y + 100.0f, panel_width - 64.0f,
        button_y - (y + 100.0f) - 18.0f};
    return layout;
}

static unsigned disabled_mask(const HelpView *view)
{
    unsigned mask = 0u;
    if (view->page <= 0) {
        mask |= 1u << HELP_FOCUS_PREVIOUS;
    }
    if (view->page >= HELP_PAGE_COUNT - 1) {
        mask |= 1u << HELP_FOCUS_NEXT;
    }
    return mask;
}

void help_view_init(HelpView *view)
{
    if (view == NULL) {
        return;
    }
    ui_button_init(&view->previous, "Previous", false);
    ui_button_init(&view->next, "Next", false);
    ui_button_init(&view->back, "Back", true);
    help_view_reset(view);
}

void help_view_reset(HelpView *view)
{
    if (view == NULL) {
        return;
    }
    view->page = 0;
    view->focus = HELP_FOCUS_NEXT;
    view->focus_visible = false;
}

HelpAction help_view_update(HelpView *view, const UiInput *input,
                            bool accept_input, float dt)
{
    if (view == NULL || input == NULL) {
        return HELP_ACTION_NONE;
    }

    const HelpLayout layout = help_layout();
    ui_button_set_bounds(&view->previous, layout.previous);
    ui_button_set_bounds(&view->next, layout.next);
    ui_button_set_bounds(&view->back, layout.back);
    const unsigned disabled = disabled_mask(view);
    view->previous.enabled = (disabled & (1u << HELP_FOCUS_PREVIOUS)) == 0;
    view->next.enabled = (disabled & (1u << HELP_FOCUS_NEXT)) == 0;

    const bool previous_clicked = ui_button_update(
        &view->previous, input, accept_input && view->previous.enabled, dt);
    const bool next_clicked = ui_button_update(
        &view->next, input, accept_input && view->next.enabled, dt);
    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return HELP_ACTION_NONE;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }
    if (input->up_pressed || input->down_pressed) {
        view->focus = ui_focus_step(
            view->focus, input->down_pressed ? 1 : -1, 3, disabled);
        view->focus_visible = true;
    }

    if (previous_clicked || (input->left_pressed && view->page > 0)) {
        if (view->page > 0) {
            --view->page;
        }
        return HELP_ACTION_NONE;
    }
    if (next_clicked
        || (input->right_pressed && view->page < HELP_PAGE_COUNT - 1)) {
        if (view->page < HELP_PAGE_COUNT - 1) {
            ++view->page;
        }
        return HELP_ACTION_NONE;
    }
    if (back_clicked || input->escape_pressed) {
        return HELP_ACTION_BACK;
    }
    if (input->enter_pressed) {
        if (view->focus == HELP_FOCUS_BACK) {
            return HELP_ACTION_BACK;
        }
        if (view->focus == HELP_FOCUS_PREVIOUS && view->page > 0) {
            --view->page;
        } else if (view->focus == HELP_FOCUS_NEXT
                   && view->page < HELP_PAGE_COUNT - 1) {
            ++view->page;
        }
    }
    return HELP_ACTION_NONE;
}

void help_view_draw(const HelpView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }

    const HelpLayout layout = help_layout();
    const HelpPage *page = &HELP_PAGES[view->page];
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "How to play",
        (Rectangle){layout.panel.x + 32.0f, layout.panel.y + 24.0f,
                    layout.panel.width - 64.0f, 36.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);

    char indicator[32];
    snprintf(indicator, sizeof(indicator), "%d / %d",
             view->page + 1, HELP_PAGE_COUNT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, indicator,
        (Rectangle){layout.panel.x + layout.panel.width - 120.0f,
                    layout.panel.y + 32.0f, 88.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_RIGHT);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, page->title,
        (Rectangle){layout.body.x, layout.body.y,
                    layout.body.width, 32.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_ACCENT, UI_ALIGN_LEFT);

    float line_y = layout.body.y + 44.0f;
    for (int index = 0; index < 10 && page->lines[index] != NULL; ++index) {
        if (page->lines[index][0] != '\0') {
            ui_draw_text_aligned(
                fonts, UI_TEXT_REGULAR, page->lines[index],
                (Rectangle){layout.body.x, line_y, layout.body.width, 24.0f},
                REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED,
                UI_ALIGN_LEFT);
        }
        line_y += 27.0f;
    }

    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == HELP_FOCUS_BACK);
    ui_button_draw(&view->previous, fonts,
                   view->focus_visible && view->focus == HELP_FOCUS_PREVIOUS);
    ui_button_draw(&view->next, fonts,
                   view->focus_visible && view->focus == HELP_FOCUS_NEXT);
}
