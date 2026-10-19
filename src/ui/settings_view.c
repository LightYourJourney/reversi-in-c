#include "ui/settings_view.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct SettingsLayout {
    Rectangle panel;
    Rectangle rows[3];
    Rectangle toggles[3];
} SettingsLayout;

static SettingsLayout settings_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(560.0f, width - 64.0f);
    const float panel_height = 420.0f;
    const float panel_x = (width - panel_width) * 0.5f;
    const float panel_y = (height - panel_height) * 0.5f;

    SettingsLayout layout = {0};
    layout.panel = (Rectangle){panel_x, panel_y,
                               panel_width, panel_height};

    const float rows_top = panel_y + 108.0f;
    for (int i = 0; i < 3; ++i) {
        layout.rows[i] = (Rectangle){
            .x = panel_x + 24.0f,
            .y = rows_top + (float)i * 66.0f,
            .width = panel_width - 48.0f,
            .height = 62.0f
        };
        layout.toggles[i] = (Rectangle){
            .x = layout.rows[i].x + layout.rows[i].width - 66.0f,
            .y = layout.rows[i].y + 17.0f,
            .width = 50.0f,
            .height = 28.0f
        };
    }
    return layout;
}

static void set_back_bounds(SettingsView *view, const SettingsLayout *layout)
{
    const Rectangle back = {
        .x = layout->panel.x + 24.0f,
        .y = layout->panel.y + layout->panel.height - 24.0f - 48.0f,
        .width = layout->panel.width - 48.0f,
        .height = 48.0f
    };
    ui_button_set_bounds(&view->back, back);
}

static void update_toggles(SettingsView *view, float dt,
                           bool animations, bool hints, bool last_move)
{
    const bool values[3] = {animations, hints, last_move};
    for (int i = 0; i < 3; ++i) {
        const float target = values[i] ? 1.0f : 0.0f;
        if (animations) {
            view->toggle_t[i] += (target - view->toggle_t[i])
                * ui_clamp01(dt * REVERSI_UI_MOTION_RATE);
        } else {
            view->toggle_t[i] = target;
        }
    }
}

static SettingsAction action_for_row(int row)
{
    if (row == 0) {
        return SETTINGS_ACTION_TOGGLE_ANIMATIONS;
    }
    if (row == 1) {
        return SETTINGS_ACTION_TOGGLE_HINTS;
    }
    return SETTINGS_ACTION_TOGGLE_LAST_MOVE;
}

void settings_view_init(SettingsView *view)
{
    if (view == NULL) {
        return;
    }

    ui_button_init(&view->back, "Back", false);
    view->focus = 0;
    view->focus_visible = false;
    view->toggle_t[0] = 1.0f;
    view->toggle_t[1] = 1.0f;
    view->toggle_t[2] = 1.0f;
}

SettingsAction settings_view_update(SettingsView *view,
                                    const UiInput *input,
                                    bool accept_input, float dt,
                                    bool animations, bool hints,
                                    bool last_move)
{
    if (view == NULL || input == NULL) {
        return SETTINGS_ACTION_NONE;
    }

    const SettingsLayout layout = settings_layout();
    set_back_bounds(view, &layout);
    update_toggles(view, dt, animations, hints, last_move);

    const bool back_clicked = ui_button_update(
        &view->back, input, accept_input, dt);

    if (!accept_input) {
        return SETTINGS_ACTION_NONE;
    }

    ui_focus_update(&view->focus, &view->focus_visible, 4, input);

    if (back_clicked || input->escape_pressed) {
        return SETTINGS_ACTION_BACK;
    }

    for (int i = 0; i < 3; ++i) {
        if (CheckCollisionPointRec(input->mouse, layout.rows[i])
            && input->mouse_pressed) {
            return action_for_row(i);
        }
    }

    if (input->enter_pressed) {
        if (view->focus == 3) {
            return SETTINGS_ACTION_BACK;
        }
        return action_for_row(view->focus);
    }

    return SETTINGS_ACTION_NONE;
}

static void draw_toggle(Rectangle track, float value)
{
    const float t = ui_clamp01(value);
    Color fill = REVERSI_UI_COLOR_SURFACE_HOVER;
    if (t > 0.0f) {
        fill = ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.35f + 0.65f * t);
    }
    DrawRectangleRounded(track, 0.5f, 18, fill);

    const float knob_radius = 10.0f;
    const float knob_x = track.x + 14.0f
        + (track.width - 28.0f) * t;
    const Vector2 knob = {knob_x, track.y + track.height * 0.5f};
    DrawCircleV((Vector2){knob.x, knob.y + 1.5f}, knob_radius,
                (Color){0, 0, 0, 50});
    DrawCircleV(knob, knob_radius,
                t > 0.5f ? REVERSI_UI_COLOR_TEXT : REVERSI_UI_COLOR_TEXT_MUTED);
}

void settings_view_draw(const SettingsView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }

    const SettingsLayout layout = settings_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, "Settings",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 26.0f,
                    layout.panel.width - 48.0f, 40.0f},
        REVERSI_UI_FONT_TITLE, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, "Make the table your own.",
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 68.0f,
                    layout.panel.width - 48.0f, 22.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    const char *labels[3] = {
        "Animations",
        "Legal move hints",
        "Last move highlight"
    };
    const char *descriptions[3] = {
        "Placement, flips and transitions",
        "Show available moves on the board",
        "Mark the most recent move"
    };
    for (int i = 0; i < 3; ++i) {
        if (view->focus_visible && view->focus == i) {
            ui_draw_rounded_rect(layout.rows[i], REVERSI_UI_RADIUS_SM,
                                 ui_color_alpha(REVERSI_UI_COLOR_ACCENT, 0.08f));
            ui_draw_rounded_outline(layout.rows[i], REVERSI_UI_RADIUS_SM, 1.0f,
                                    ui_color_alpha(REVERSI_UI_COLOR_FOCUS, 0.65f));
        }

        ui_draw_text_aligned(
            fonts, UI_TEXT_SEMIBOLD, labels[i],
            (Rectangle){layout.rows[i].x, layout.rows[i].y + 8.0f,
                        layout.rows[i].width - 90.0f, 24.0f},
            REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
        ui_draw_text_aligned(
            fonts, UI_TEXT_REGULAR, descriptions[i],
            (Rectangle){layout.rows[i].x, layout.rows[i].y + 32.0f,
                        layout.rows[i].width - 90.0f, 20.0f},
            REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

        draw_toggle(layout.toggles[i], view->toggle_t[i]);

        if (i < 2) {
            DrawLineEx(
                (Vector2){layout.rows[i].x,
                          layout.rows[i].y + layout.rows[i].height},
                (Vector2){layout.rows[i].x + layout.rows[i].width,
                          layout.rows[i].y + layout.rows[i].height},
                1.0f, REVERSI_UI_COLOR_BORDER);
        }
    }

    ui_button_draw(&view->back, fonts,
                   view->focus_visible && view->focus == 3);
}
