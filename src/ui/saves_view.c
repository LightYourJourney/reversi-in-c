#include "ui/saves_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define REVERSI_SAVES_FOCUS_BACK REVERSI_SAVES_ROW_COUNT

typedef struct SavesLayout {
    Rectangle panel;
    Rectangle rows[REVERSI_SAVES_ROW_COUNT];
    Rectangle actions[REVERSI_SAVES_ROW_COUNT];
    Rectangle deletes[REVERSI_SAVES_ROW_COUNT];
    Rectangle back;
} SavesLayout;

static SavesLayout saves_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(640.0f, width - 64.0f);
    const float panel_height = fminf(536.0f, height - 64.0f);
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float rows_top = y + 96.0f;

    SavesLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        layout.rows[row] = (Rectangle){
            x + 24.0f, rows_top + (float)row * 72.0f,
            panel_width - 48.0f, 64.0f};
        layout.actions[row] = (Rectangle){
            layout.rows[row].x + layout.rows[row].width - 92.0f,
            layout.rows[row].y + 14.0f, 92.0f, 36.0f};
        layout.deletes[row] = (Rectangle){
            layout.actions[row].x - 8.0f - 68.0f,
            layout.rows[row].y + 14.0f, 68.0f, 36.0f};
    }
    layout.back = (Rectangle){
        x + 24.0f, y + panel_height - 24.0f - 44.0f,
        panel_width - 48.0f, 44.0f};
    return layout;
}

static bool action_enabled(bool saving, int row, const SaveSlotInfo *slot)
{
    if (saving) {
        return row > 0;
    }
    return slot->present && slot->valid;
}

static bool delete_enabled(bool saving, int row, const SaveSlotInfo *slot)
{
    if (saving && row == 0) {
        return false;
    }
    return slot->present;
}

static unsigned disabled_mask(bool saving)
{
    /* In save mode the autosave row has no action; all other rows stay
       focusable so Delete can reach present-but-corrupt saves. */
    return saving ? 1u : 0u;
}

void saves_view_init(SavesView *view)
{
    if (view == NULL) {
        return;
    }
    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        ui_button_init(&view->actions[row], "Load", false);
        ui_button_init(&view->deletes[row], "Delete", false);
    }
    ui_button_init(&view->back, "Back", false);
    view->focus = 1;
    view->focus_visible = false;
}

SavesAction saves_view_update(
    SavesView *view, const UiInput *input, bool accept_input, float dt,
    bool saving, const SaveSlotInfo slots[REVERSI_SAVES_ROW_COUNT])
{
    SavesAction result = {SAVES_ACTION_NONE, 0};
    if (view == NULL || input == NULL || slots == NULL) {
        return result;
    }

    const SavesLayout layout = saves_layout();
    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        UiButton *action = &view->actions[row];
        UiButton *remove = &view->deletes[row];
        action->label = saving ? "Save" : "Load";
        ui_button_set_bounds(action, layout.actions[row]);
        ui_button_set_bounds(remove, layout.deletes[row]);
        action->enabled = action_enabled(saving, row, &slots[row]);
        remove->enabled = delete_enabled(saving, row, &slots[row]);
    }
    ui_button_set_bounds(&view->back, layout.back);

    bool action_clicked[REVERSI_SAVES_ROW_COUNT] = {false};
    bool delete_clicked[REVERSI_SAVES_ROW_COUNT] = {false};
    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        action_clicked[row] = ui_button_update(
            &view->actions[row], input,
            accept_input && view->actions[row].enabled, dt);
        delete_clicked[row] = ui_button_update(
            &view->deletes[row], input,
            accept_input && view->deletes[row].enabled, dt);
    }
    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return result;
    }

    if (input->mouse_pressed) {
        view->focus_visible = false;
    }
    const unsigned disabled = disabled_mask(saving);
    if (input->up_pressed || input->down_pressed) {
        view->focus = ui_focus_step(
            view->focus, input->down_pressed ? 1 : -1,
            REVERSI_SAVES_ROW_COUNT + 1, disabled);
        view->focus_visible = true;
    }

    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        if (delete_clicked[row]) {
            result.type = SAVES_ACTION_DELETE;
            result.row = row;
            return result;
        }
        if (action_clicked[row]) {
            result.type = saving ? SAVES_ACTION_SAVE : SAVES_ACTION_LOAD;
            result.row = row;
            return result;
        }
    }

    if (back_clicked || input->escape_pressed) {
        result.type = SAVES_ACTION_BACK;
        return result;
    }

    const int focus = view->focus;
    if (input->delete_pressed && focus < REVERSI_SAVES_ROW_COUNT
        && delete_enabled(saving, focus, &slots[focus])) {
        result.type = SAVES_ACTION_DELETE;
        result.row = focus;
        return result;
    }
    if (input->enter_pressed) {
        if (focus == REVERSI_SAVES_FOCUS_BACK) {
            result.type = SAVES_ACTION_BACK;
            return result;
        }
        if (action_enabled(saving, focus, &slots[focus])) {
            result.type = saving ? SAVES_ACTION_SAVE : SAVES_ACTION_LOAD;
            result.row = focus;
        }
    }
    return result;
}

static const char *row_label(int row)
{
    switch (row) {
    case 0:
        return "Autosave";
    case 1:
        return "Slot 1";
    case 2:
        return "Slot 2";
    default:
        return "Slot 3";
    }
}

void saves_view_draw(
    const SavesView *view, const UiFonts *fonts, bool saving,
    const SaveSlotInfo slots[REVERSI_SAVES_ROW_COUNT])
{
    if (view == NULL || fonts == NULL || slots == NULL) {
        return;
    }

    const SavesLayout layout = saves_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, saving ? "Save game" : "Load game",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 22.0f,
                    layout.panel.width - 48.0f, 36.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR,
        saving ? "Choose a slot. Existing data is replaced."
               : "Autosave is written after every move.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 62.0f,
                    layout.panel.width - 48.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        const Rectangle bounds = layout.rows[row];
        if (view->focus_visible && view->focus == row) {
            ui_draw_rounded_rect(bounds, REVERSI_UI_RADIUS_SM,
                                 ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.08f));
            ui_draw_rounded_outline(bounds, REVERSI_UI_RADIUS_SM, 1.0f,
                                    ui_color_alpha(REVERSI_UI_COLOR_FOCUS, 0.65f));
        }

        ui_draw_text_aligned(
            fonts, UI_TEXT_SEMIBOLD, row_label(row),
            (Rectangle){bounds.x + 12.0f, bounds.y + 7.0f, 180.0f, 24.0f},
            REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);

        const char *summary = slots[row].summary;
        if (summary[0] == '\0') {
            summary = saving && row == 0 ? "Managed automatically" : "Empty";
        }
        ui_draw_text_aligned(
            fonts, UI_TEXT_REGULAR, summary,
            (Rectangle){bounds.x + 12.0f, bounds.y + 31.0f,
                        layout.deletes[row].x - bounds.x - 20.0f, 20.0f},
            REVERSI_UI_FONT_TINY,
            slots[row].valid ? REVERSI_UI_COLOR_TEXT_MUTED
                             : REVERSI_UI_COLOR_TEXT_FAINT,
            UI_ALIGN_LEFT);

        ui_button_draw(&view->actions[row], fonts,
                       view->focus_visible && view->focus == row);
        ui_button_draw(&view->deletes[row], fonts, false);
    }

    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == REVERSI_SAVES_FOCUS_BACK);
}
