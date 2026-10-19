#include "ui/history_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct HistoryLayout {
    Rectangle panel;
    Rectangle list;
    Rectangle back;
    float row_height;
    int visible_rows;
} HistoryLayout;

static HistoryLayout history_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(640.0f, width - 64.0f);
    const float panel_height = fminf(520.0f, height - 64.0f);
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float button_height = 46.0f;
    const Rectangle back = {
        x + 24.0f, y + panel_height - 24.0f - button_height,
        panel_width - 48.0f, button_height
    };
    const float list_top = y + 96.0f;
    const float list_bottom = back.y - 28.0f;
    const float row_height = 32.0f;
    const int visible = (int)fmaxf(1.0f,
                                   floorf((list_bottom - list_top)
                                          / row_height));

    HistoryLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.list = (Rectangle){x + 18.0f, list_top,
                              panel_width - 36.0f,
                              (float)visible * row_height};
    layout.back = back;
    layout.row_height = row_height;
    layout.visible_rows = visible;
    return layout;
}

static int max_scroll(const Match *match, int visible_rows)
{
    const int count = history_count(&match->history);
    return count > visible_rows ? count - visible_rows : 0;
}

static int clamp_scroll(int scroll, const Match *match, int visible_rows)
{
    if (scroll < 0) {
        scroll = 0;
    }
    const int max = max_scroll(match, visible_rows);
    if (scroll > max) {
        scroll = max;
    }
    return scroll;
}

void history_view_init(HistoryView *view)
{
    if (view == NULL) {
        return;
    }

    view->scroll = 0;
    ui_button_init(&view->back, "Back", false);
}

void history_view_open(HistoryView *view, const Match *match)
{
    if (view == NULL || match == NULL) {
        return;
    }

    const HistoryLayout layout = history_layout();
    const int current = history_position(&match->history);
    view->scroll = current - layout.visible_rows + 1;
    view->scroll = clamp_scroll(view->scroll, match, layout.visible_rows);
}

HistoryAction history_view_update(HistoryView *view, const Match *match,
                                  const UiInput *input,
                                  bool accept_input, float dt)
{
    if (view == NULL || match == NULL || input == NULL) {
        return HISTORY_ACTION_NONE;
    }

    const HistoryLayout layout = history_layout();
    ui_button_set_bounds(&view->back, layout.back);
    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    view->scroll = clamp_scroll(view->scroll, match, layout.visible_rows);

    if (!accept_input) {
        return HISTORY_ACTION_NONE;
    }

    if (input->wheel > 0.0f) {
        view->scroll -= (int)ceilf(input->wheel);
    } else if (input->wheel < 0.0f) {
        view->scroll += (int)ceilf(-input->wheel);
    }
    if (input->up_pressed) {
        --view->scroll;
    }
    if (input->down_pressed) {
        ++view->scroll;
    }
    view->scroll = clamp_scroll(view->scroll, match, layout.visible_rows);

    if (back_clicked || input->escape_pressed || input->enter_pressed) {
        return HISTORY_ACTION_BACK;
    }
    return HISTORY_ACTION_NONE;
}

void history_view_draw(const HistoryView *view, const Match *match,
                       const UiFonts *fonts)
{
    if (view == NULL || match == NULL || fonts == NULL) {
        return;
    }

    const HistoryLayout layout = history_layout();
    const int count = history_count(&match->history);
    const int current = history_position(&match->history);

    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "History",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 22.0f,
                    layout.panel.width - 48.0f, 38.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    char subtitle[96];
    snprintf(subtitle, sizeof(subtitle),
             "%d plies  |  viewing position %d of %d",
             count > 0 ? count - 1 : 0, current < 0 ? 0 : current,
             count > 0 ? count - 1 : 0);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, subtitle,
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 62.0f,
                    layout.panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    const int first = view->scroll;
    const int last = count < first + layout.visible_rows
        ? count : first + layout.visible_rows;
    for (int index = first; index < last; ++index) {
        const HistoryEntry *entry = history_entry(&match->history, index);
        if (entry == NULL) {
            continue;
        }

        const float row_y = layout.list.y
            + (float)(index - first) * layout.row_height;
        const Rectangle row = {
            layout.list.x, row_y, layout.list.width, layout.row_height
        };
        const bool is_current = index == current;
        const bool is_redo = index > current;

        if (is_current) {
            ui_draw_rounded_rect(row, 6.0f,
                                 ui_color_alpha(REVERSI_UI_COLOR_ACCENT,
                                                0.10f));
            ui_draw_rounded_rect(
                (Rectangle){row.x + 1.0f, row.y + 6.0f, 3.0f,
                            row.height - 12.0f},
                1.5f, REVERSI_UI_COLOR_ACCENT);
        }

        char label[128];
        if (!entry->has_move) {
            snprintf(label, sizeof(label), "00   Start position");
        } else {
            char notation[3] = "??";
            (void)move_to_notation(entry->move, notation);
            snprintf(label, sizeof(label), "%02d   %s   %s",
                     index, ui_player_name(entry->mover), notation);
            size_t used = strlen(label);
            if (entry->flips > 0) {
                snprintf(label + used, sizeof(label) - used,
                         "   |   %d flip%s", entry->flips,
                         entry->flips == 1 ? "" : "s");
                used = strlen(label);
            }
            if (entry->pass.occurred) {
                snprintf(label + used, sizeof(label) - used,
                         "   |   %s passed",
                         ui_player_name(entry->pass.player));
            }
        }

        Color color = REVERSI_UI_COLOR_TEXT;
        if (is_redo) {
            color = REVERSI_UI_COLOR_TEXT_FAINT;
        } else if (!is_current) {
            color = REVERSI_UI_COLOR_TEXT_MUTED;
        }
        ui_draw_text_clipped(
            fonts, UI_TEXT_REGULAR, label,
            (Rectangle){row.x + 12.0f, row.y, row.width - 100.0f,
                        row.height},
            REVERSI_UI_FONT_SMALL, color, UI_ALIGN_LEFT);

        const char *marker = is_current ? "CURRENT"
            : (is_redo ? "REDO" : NULL);
        if (marker != NULL) {
            ui_draw_text_aligned(
                fonts, UI_TEXT_SEMIBOLD, marker,
                (Rectangle){row.x + row.width - 78.0f, row.y, 66.0f,
                            row.height},
                REVERSI_UI_FONT_TINY,
                is_current ? REVERSI_UI_COLOR_ACCENT
                           : REVERSI_UI_COLOR_TEXT_FAINT,
                UI_ALIGN_RIGHT);
        }
    }

    if (count > layout.visible_rows) {
        const float track_x = layout.panel.x + layout.panel.width - 12.0f;
        const float track_y = layout.list.y + 2.0f;
        const float track_height = layout.list.height - 4.0f;
        const float thumb_height = fmaxf(22.0f, track_height
            * (float)layout.visible_rows / (float)count);
        const int max = max_scroll(match, layout.visible_rows);
        const float travel = track_height - thumb_height;
        const float thumb_y = track_y + (max > 0
            ? travel * (float)view->scroll / (float)max : 0.0f);
        DrawRectangleRounded(
            (Rectangle){track_x, track_y, 4.0f, track_height},
            0.5f, 8, ui_color_alpha(REVERSI_UI_COLOR_BORDER_STRONG,
                                    0.35f));
        DrawRectangleRounded(
            (Rectangle){track_x, thumb_y, 4.0f, thumb_height},
            0.5f, 8, ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.55f));
    }

    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "Up/Down or mouse wheel to scroll",
        (Rectangle){layout.panel.x + 24.0f, layout.back.y - 22.0f,
                    layout.panel.width - 48.0f, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);

    ui_button_draw(&view->back, fonts, false);
}
