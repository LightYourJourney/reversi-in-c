#include "ui/stats_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

typedef struct StatsLayout {
    Rectangle panel;
    Rectangle back;
} StatsLayout;

static StatsLayout stats_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(600.0f, width - 64.0f);
    const float panel_height = fminf(500.0f, height - 64.0f);
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;

    StatsLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.back = (Rectangle){
        x + 24.0f, y + panel_height - 24.0f - 44.0f,
        panel_width - 48.0f, 44.0f};
    return layout;
}

void stats_view_init(StatsView *view)
{
    if (view == NULL) {
        return;
    }
    ui_button_init(&view->back, "Back", true);
    view->focus = 0;
    view->focus_visible = false;
}

StatsAction stats_view_update(StatsView *view, const UiInput *input,
                              bool accept_input, float dt)
{
    if (view == NULL || input == NULL) {
        return STATS_ACTION_NONE;
    }

    const StatsLayout layout = stats_layout();
    ui_button_set_bounds(&view->back, layout.back);
    const bool clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return STATS_ACTION_NONE;
    }
    if (clicked || input->escape_pressed || input->enter_pressed) {
        return STATS_ACTION_BACK;
    }
    return STATS_ACTION_NONE;
}

static void draw_label_value(const UiFonts *fonts, Rectangle row,
                             const char *label, const char *value,
                             Color value_color)
{
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, label,
        (Rectangle){row.x, row.y, row.width * 0.6f, row.height},
        REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, value,
        (Rectangle){row.x + row.width * 0.55f, row.y,
                    row.width * 0.45f, row.height},
        REVERSI_UI_FONT_BODY, value_color, UI_ALIGN_RIGHT);
}

void stats_view_draw(const StatsView *view, const UiFonts *fonts,
                     const GameStats *stats)
{
    if (view == NULL || fonts == NULL || stats == NULL) {
        return;
    }

    const StatsLayout layout = stats_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "Statistics",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 22.0f,
                    layout.panel.width - 48.0f, 36.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "Completed games only.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 62.0f,
                    layout.panel.width - 48.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    char value[64];
    const Rectangle first = {
        layout.panel.x + 24.0f, layout.panel.y + 100.0f,
        layout.panel.width - 48.0f, 26.0f};

    snprintf(value, sizeof(value), "%d", stats_total_games(stats));
    draw_label_value(fonts, first, "Total games", value,
                     REVERSI_UI_COLOR_TEXT);

    snprintf(value, sizeof(value), "%d W  -  %d D  -  %d L",
             stats_human_wins(stats), stats_human_draws(stats),
             stats_human_losses(stats));
    Rectangle row = first;
    row.y += 34.0f;
    draw_label_value(fonts, row, "Your record vs computer", value,
                     REVERSI_UI_COLOR_ACCENT);

    snprintf(value, sizeof(value), "%d game%s  (%d draw%s)",
             stats->pvp_games, stats->pvp_games == 1 ? "" : "s",
             stats->pvp_draws, stats->pvp_draws == 1 ? "" : "s");
    row.y += 34.0f;
    draw_label_value(fonts, row, "Local two-player", value,
                     REVERSI_UI_COLOR_TEXT);

    const float table_y = layout.panel.y + 214.0f;
    DrawLineEx(
        (Vector2){layout.panel.x + 24.0f, table_y - 16.0f},
        (Vector2){layout.panel.x + layout.panel.width - 24.0f,
                  table_y - 16.0f},
        1.0f, REVERSI_UI_COLOR_BORDER);

    const char *headers[5] = {
        "Difficulty", "Games", "Wins", "Draws", "Losses"
    };
    const float column_width = (layout.panel.width - 48.0f) / 5.0f;
    for (int index = 0; index < 5; ++index) {
        ui_draw_text_aligned(
            fonts, UI_TEXT_REGULAR, headers[index],
            (Rectangle){layout.panel.x + 24.0f + column_width * (float)index,
                        table_y, column_width, 20.0f},
            REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
            index == 0 ? UI_ALIGN_LEFT : UI_ALIGN_RIGHT);
    }

    const char *names[REVERSI_STATS_DIFFICULTY_COUNT] = {
        "Easy", "Medium", "Hard"
    };
    for (int difficulty = 0;
         difficulty < REVERSI_STATS_DIFFICULTY_COUNT; ++difficulty) {
        const DifficultyStats *record = &stats->pve[difficulty];
        const float y = table_y + 34.0f + (float)difficulty * 34.0f;
        const char *cells[5];
        char games[16];
        char wins[16];
        char draws[16];
        char losses[16];
        snprintf(games, sizeof(games), "%d", record->games);
        snprintf(wins, sizeof(wins), "%d", record->wins);
        snprintf(draws, sizeof(draws), "%d", record->draws);
        snprintf(losses, sizeof(losses), "%d", record->losses);
        cells[0] = names[difficulty];
        cells[1] = games;
        cells[2] = wins;
        cells[3] = draws;
        cells[4] = losses;
        for (int index = 0; index < 5; ++index) {
            ui_draw_text_aligned(
                fonts,
                index == 0 ? UI_TEXT_SEMIBOLD : UI_TEXT_REGULAR,
                cells[index],
                (Rectangle){layout.panel.x + 24.0f
                                + column_width * (float)index,
                            y, column_width, 24.0f},
                REVERSI_UI_FONT_BODY,
                index == 0 ? REVERSI_UI_COLOR_TEXT
                           : REVERSI_UI_COLOR_TEXT_MUTED,
                index == 0 ? UI_ALIGN_LEFT : UI_ALIGN_RIGHT);
        }
    }

    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR,
        "Draws are included in games and are not counted as wins or losses.",
        (Rectangle){layout.panel.x + 24.0f,
                    layout.back.y - 34.0f,
                    layout.panel.width - 48.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);

    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == 0);
}
