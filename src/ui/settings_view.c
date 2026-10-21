#include "ui/settings_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define REVERSI_SETTINGS_ROW_COUNT 8
#define REVERSI_SETTINGS_FOCUS_BACK REVERSI_SETTINGS_ROW_COUNT

typedef enum SettingsRow {
    ROW_ANIMATIONS = 0,
    ROW_SPEED,
    ROW_HINTS,
    ROW_LAST_MOVE,
    ROW_SFX,
    ROW_VOLUME,
    ROW_DIFFICULTY,
    ROW_THEME
} SettingsRow;

typedef struct SettingsLayout {
    Rectangle panel;
    Rectangle rows[REVERSI_SETTINGS_ROW_COUNT];
} SettingsLayout;

static SettingsLayout settings_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(600.0f, width - 64.0f);
    const float panel_height = fminf(536.0f, height - 64.0f);
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    SettingsLayout layout = {0};
    layout.panel = (Rectangle){panel_x, panel_y,
                               panel_width, panel_height};

    const float rows_top = panel_y + 96.0f;
    for (int i = 0; i < REVERSI_SETTINGS_ROW_COUNT; ++i) {
        layout.rows[i] = (Rectangle){
            .x = panel_x + 24.0f,
            .y = rows_top + (float)i * 44.0f,
            .width = panel_width - 48.0f,
            .height = 40.0f
        };
    }
    return layout;
}

static void set_back_bounds(SettingsView *view, const SettingsLayout *layout)
{
    const Rectangle back = {
        .x = layout->panel.x + 24.0f,
        .y = layout->panel.y + layout->panel.height - 24.0f - 44.0f,
        .width = layout->panel.width - 48.0f,
        .height = 44.0f
    };
    ui_button_set_bounds(&view->back, back);
}

static bool row_is_toggle(int row)
{
    return row == ROW_ANIMATIONS || row == ROW_HINTS
        || row == ROW_LAST_MOVE || row == ROW_SFX;
}

static int toggle_index(int row)
{
    switch (row) {
    case ROW_ANIMATIONS:
        return 0;
    case ROW_HINTS:
        return 1;
    case ROW_LAST_MOVE:
        return 2;
    case ROW_SFX:
    default:
        return 3;
    }
}

static bool toggle_value(const Settings *settings, int row)
{
    switch (row) {
    case ROW_ANIMATIONS:
        return settings->animations;
    case ROW_HINTS:
        return settings->legal_hints;
    case ROW_LAST_MOVE:
        return settings->last_move_highlight;
    case ROW_SFX:
    default:
        return settings->sfx;
    }
}

static void update_toggles(SettingsView *view, float dt,
                           const Settings *settings)
{
    for (int row = 0; row < REVERSI_SETTINGS_ROW_COUNT; ++row) {
        if (!row_is_toggle(row)) {
            continue;
        }
        const int index = toggle_index(row);
        const float target = toggle_value(settings, row) ? 1.0f : 0.0f;
        if (settings->animations) {
            view->toggle_t[index] += (target - view->toggle_t[index])
                * ui_clamp01(dt * REVERSI_UI_MOTION_RATE);
        } else {
            view->toggle_t[index] = target;
        }
    }
}

static SettingsAction action_for_row(int row, bool forward)
{
    switch (row) {
    case ROW_ANIMATIONS:
        return SETTINGS_ACTION_TOGGLE_ANIMATIONS;
    case ROW_SPEED:
        return forward ? SETTINGS_ACTION_SPEED_UP
                       : SETTINGS_ACTION_SPEED_DOWN;
    case ROW_HINTS:
        return SETTINGS_ACTION_TOGGLE_HINTS;
    case ROW_LAST_MOVE:
        return SETTINGS_ACTION_TOGGLE_LAST_MOVE;
    case ROW_SFX:
        return SETTINGS_ACTION_TOGGLE_SFX;
    case ROW_VOLUME:
        return forward ? SETTINGS_ACTION_VOLUME_UP
                       : SETTINGS_ACTION_VOLUME_DOWN;
    case ROW_DIFFICULTY:
        return forward ? SETTINGS_ACTION_DIFFICULTY_UP
                       : SETTINGS_ACTION_DIFFICULTY_DOWN;
    case ROW_THEME:
    default:
        return forward ? SETTINGS_ACTION_THEME_UP
                       : SETTINGS_ACTION_THEME_DOWN;
    }
}

void settings_view_init(SettingsView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->back, "Back", false);
    view->focus = 0;
    view->focus_visible = false;
    for (int index = 0; index < 4; ++index) {
        view->toggle_t[index] = 1.0f;
    }
}

SettingsAction settings_view_update(SettingsView *view,
                                    const UiInput *input,
                                    bool accept_input, float dt,
                                    const Settings *settings)
{
    if (view == NULL || input == NULL || settings == NULL) {
        return SETTINGS_ACTION_NONE;
    }

    const SettingsLayout layout = settings_layout();
    set_back_bounds(view, &layout);
    update_toggles(view, dt, settings);

    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return SETTINGS_ACTION_NONE;
    }

    ui_focus_update(&view->focus, &view->focus_visible,
                    REVERSI_SETTINGS_ROW_COUNT + 1, input);

    if (back_clicked || input->escape_pressed) {
        return SETTINGS_ACTION_BACK;
    }

    for (int row = 0; row < REVERSI_SETTINGS_ROW_COUNT; ++row) {
        if (CheckCollisionPointRec(input->mouse, layout.rows[row])
            && input->mouse_pressed) {
            return action_for_row(row, true);
        }
    }

    if ((input->left_pressed || input->right_pressed)
        && view->focus_visible && view->focus < REVERSI_SETTINGS_ROW_COUNT) {
        return action_for_row(view->focus, input->right_pressed);
    }

    if (input->enter_pressed) {
        if (view->focus == REVERSI_SETTINGS_FOCUS_BACK) {
            return SETTINGS_ACTION_BACK;
        }
        return action_for_row(view->focus, true);
    }

    return SETTINGS_ACTION_NONE;
}

static const char *difficulty_label(int difficulty)
{
    switch (difficulty) {
    case 0:
        return "Easy";
    case 2:
        return "Hard";
    case 1:
    default:
        return "Medium";
    }
}

static void draw_toggle(Rectangle track, float value)
{
    const float t = ui_clamp01(value);
    Color fill = REVERSI_UI_COLOR_SURFACE_HOVER;
    if (t > 0.0f) {
        fill = ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.35f + 0.65f * t);
    }
    DrawRectangleRounded(track, 0.5f, 18, fill);

    const float knob_radius = 9.0f;
    const float knob_x = track.x + 13.0f
        + (track.width - 26.0f) * t;
    const Vector2 knob = {knob_x, track.y + track.height * 0.5f};
    DrawCircleV((Vector2){knob.x, knob.y + 1.5f}, knob_radius,
                (Color){0, 0, 0, 50});
    DrawCircleV(knob, knob_radius,
                t > 0.5f ? REVERSI_UI_COLOR_ACCENT_TEXT
                         : REVERSI_UI_COLOR_TEXT_MUTED);
}

static void row_value_text(const Settings *settings, int row,
                           char *out, size_t cap)
{
    switch (row) {
    case ROW_SPEED:
        snprintf(out, cap, "%d%%", settings->animation_speed);
        break;
    case ROW_VOLUME:
        snprintf(out, cap, "%d%%", settings->volume);
        break;
    case ROW_DIFFICULTY:
        snprintf(out, cap, "%s",
                 difficulty_label(settings->default_difficulty));
        break;
    case ROW_THEME:
        snprintf(out, cap, "%s",
                 ui_theme_name((UiThemeKind)settings->theme));
        break;
    default:
        out[0] = '\0';
        break;
    }
}

void settings_view_draw(const SettingsView *view, const UiFonts *fonts,
                        const Settings *settings)
{
    if (view == NULL || fonts == NULL || settings == NULL) {
        return;
    }

    const SettingsLayout layout = settings_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "Settings",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 22.0f,
                    layout.panel.width - 48.0f, 36.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR,
        "Changes are saved automatically.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 62.0f,
                    layout.panel.width - 48.0f, 20.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    const char *labels[REVERSI_SETTINGS_ROW_COUNT] = {
        "Animations",
        "Animation speed",
        "Legal move hints",
        "Last move highlight",
        "Sound effects",
        "Volume",
        "Default AI difficulty",
        "Theme"
    };

    for (int row = 0; row < REVERSI_SETTINGS_ROW_COUNT; ++row) {
        if (view->focus_visible && view->focus == row) {
            ui_draw_rounded_rect(layout.rows[row], REVERSI_UI_RADIUS_SM,
                                 ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.08f));
            ui_draw_rounded_outline(layout.rows[row], REVERSI_UI_RADIUS_SM, 1.0f,
                                    ui_color_alpha(REVERSI_UI_COLOR_FOCUS, 0.65f));
        }

        ui_draw_text_aligned(
            fonts, UI_TEXT_SEMIBOLD, labels[row],
            (Rectangle){layout.rows[row].x + 10.0f,
                        layout.rows[row].y,
                        layout.rows[row].width * 0.58f,
                        layout.rows[row].height},
            REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);

        if (row_is_toggle(row)) {
            const Rectangle track = {
                .x = layout.rows[row].x + layout.rows[row].width - 58.0f,
                .y = layout.rows[row].y + 8.0f,
                .width = 46.0f,
                .height = 24.0f
            };
            draw_toggle(track, view->toggle_t[toggle_index(row)]);
        } else {
            char value[32];
            row_value_text(settings, row, value, sizeof(value));
            ui_draw_text_aligned(
                fonts, UI_TEXT_REGULAR, value,
                (Rectangle){layout.rows[row].x
                                + layout.rows[row].width * 0.58f,
                            layout.rows[row].y,
                            layout.rows[row].width * 0.40f,
                            layout.rows[row].height},
                REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT_MUTED,
                UI_ALIGN_RIGHT);
        }
    }

    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == REVERSI_SETTINGS_FOCUS_BACK);
}
