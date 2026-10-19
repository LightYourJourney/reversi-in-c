#include "ui/draw.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

static float ui_roundness(Rectangle rect, float radius)
{
    const float min_side = fminf(rect.width, rect.height);
    if (min_side <= 0.0f) {
        return 0.0f;
    }
    return ui_clamp01(radius / (min_side * 0.5f));
}

static Font ui_font_for(const UiFonts *fonts, UiTextWeight weight)
{
    return weight == UI_TEXT_SEMIBOLD
        ? ui_font_semibold(fonts)
        : ui_font_regular(fonts);
}

static float ui_spacing_for(float size)
{
    return size * 0.015f;
}

float ui_clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float ui_ease_out_back(float value)
{
    const float t = ui_clamp01(value);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    const float shifted = t - 1.0f;
    return 1.0f + c3 * shifted * shifted * shifted
        + c1 * shifted * shifted;
}

Color ui_color_alpha(Color color, float alpha)
{
    Color result = color;
    result.a = (unsigned char)((float)color.a * ui_clamp01(alpha));
    return result;
}

void ui_focus_update(int *focus, bool *visible, int count,
                     const UiInput *input)
{
    if (focus == NULL || visible == NULL || input == NULL || count <= 0) {
        return;
    }

    if (input->mouse_pressed) {
        *visible = false;
    }
    if (input->up_pressed || input->down_pressed) {
        *focus = (*focus + (input->down_pressed ? 1 : -1) + count) % count;
        *visible = true;
    }
}

const char *ui_player_name(Stone stone)
{
    if (stone == STONE_BLACK) {
        return "Black";
    }
    if (stone == STONE_WHITE) {
        return "White";
    }
    return "None";
}

const char *ui_result_title(GameResult result)
{
    switch (result) {
    case GAME_RESULT_BLACK_WIN:
        return "Black wins";
    case GAME_RESULT_WHITE_WIN:
        return "White wins";
    case GAME_RESULT_DRAW:
        return "Draw";
    case GAME_RESULT_ONGOING:
    default:
        return NULL;
    }
}

int ui_focus_step(int focus, int direction, int count, int disabled_index)
{
    if (count <= 0) {
        return focus;
    }

    const int step = direction < 0 ? -1 : 1;
    for (int attempt = 0; attempt < count; ++attempt) {
        focus = (focus + step + count) % count;
        if (focus != disabled_index) {
            return focus;
        }
    }
    return focus;
}

void ui_draw_background(void)
{
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           REVERSI_UI_COLOR_BG_TOP, REVERSI_UI_COLOR_BG_BOTTOM);
}

void ui_draw_shadow(Rectangle rect, float radius, float offset, float alpha)
{
    const float strength = ui_clamp01(alpha);
    for (int i = 0; i < 3; ++i) {
        const float spread = (float)i * 2.0f;
        Rectangle layer = {
            .x = rect.x - spread,
            .y = rect.y + offset - spread,
            .width = rect.width + spread * 2.0f,
            .height = rect.height + spread * 2.0f
        };
        const float layer_alpha = strength * (0.32f - (float)i * 0.07f);
        DrawRectangleRounded(layer, ui_roundness(layer, radius + spread),
                             18, ui_color_alpha(REVERSI_UI_COLOR_SHADOW,
                                                layer_alpha));
    }
}

void ui_draw_rounded_rect(Rectangle rect, float radius, Color color)
{
    DrawRectangleRounded(rect, ui_roundness(rect, radius), 18, color);
}

void ui_draw_rounded_outline(Rectangle rect, float radius, float thickness,
                             Color color)
{
    DrawRectangleRoundedLinesEx(rect, ui_roundness(rect, radius), 18,
                                thickness, color);
}

void ui_draw_panel(Rectangle rect, float radius, Color fill, Color border)
{
    ui_draw_shadow(rect, radius, 8.0f, 0.28f);
    ui_draw_rounded_rect(rect, radius, fill);
    if (border.a > 0) {
        ui_draw_rounded_outline(rect, radius, 1.0f, border);
    }
}

void ui_draw_text_aligned(const UiFonts *fonts, UiTextWeight weight,
                          const char *text, Rectangle box, float size,
                          Color color, UiAlign align)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    const Font font = ui_font_for(fonts, weight);
    const float spacing = ui_spacing_for(size);
    const Vector2 measured = MeasureTextEx(font, text, size, spacing);

    float x = box.x;
    if (align == UI_ALIGN_CENTER) {
        x = box.x + (box.width - measured.x) * 0.5f;
    } else if (align == UI_ALIGN_RIGHT) {
        x = box.x + box.width - measured.x;
    }
    const float y = box.y + (box.height - measured.y) * 0.5f;

    DrawTextEx(font, text, (Vector2){x, y}, size, spacing, color);
}

void ui_draw_text_clipped(const UiFonts *fonts, UiTextWeight weight,
                          const char *text, Rectangle box, float size,
                          Color color, UiAlign align)
{
    if (text == NULL || text[0] == '\0'
        || box.width <= 0.0f || box.height <= 0.0f) {
        return;
    }

    BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);
    ui_draw_text_aligned(fonts, weight, text, box, size, color, align);
    EndScissorMode();
}

void ui_draw_disc(Vector2 center, float radius, Stone stone,
                  float alpha, float scale_x, float scale_y)
{
    if (stone != STONE_BLACK && stone != STONE_WHITE) {
        return;
    }

    const float rx = radius * scale_x;
    const float ry = radius * scale_y;
    if (rx < 0.4f || ry < 0.4f) {
        return;
    }

    const Color fill = stone == STONE_BLACK
        ? REVERSI_UI_COLOR_BLACK_DISC : REVERSI_UI_COLOR_WHITE_DISC;
    const Color rim = stone == STONE_BLACK
        ? REVERSI_UI_COLOR_BLACK_RIM : REVERSI_UI_COLOR_WHITE_RIM;

    DrawEllipseV((Vector2){center.x, center.y + radius * 0.10f},
                 rx * 0.98f, ry * 0.98f,
                 ui_color_alpha(REVERSI_UI_COLOR_SHADOW, alpha * 0.35f));
    DrawEllipseV(center, rx + 1.5f, ry + 1.5f,
                 ui_color_alpha(rim, alpha));
    DrawEllipseV(center, rx, ry, ui_color_alpha(fill, alpha));

    if (stone == STONE_BLACK) {
        DrawEllipseV((Vector2){center.x - rx * 0.28f,
                               center.y - ry * 0.30f},
                     rx * 0.34f, ry * 0.24f,
                     ui_color_alpha(REVERSI_UI_COLOR_HIGHLIGHT,
                                    alpha * 0.07f));
    } else {
        DrawEllipseV((Vector2){center.x - rx * 0.26f,
                               center.y - ry * 0.28f},
                     rx * 0.42f, ry * 0.30f,
                     ui_color_alpha(REVERSI_UI_COLOR_HIGHLIGHT,
                                    alpha * 0.35f));
    }
}

void ui_draw_ring(Vector2 center, float radius, float thickness, Color color)
{
    const float half = thickness * 0.5f;
    const float inner = fmaxf(0.0f, radius - half);
    DrawRing(center, inner, radius + half, 0.0f, 360.0f, 48, color);
}

void ui_button_init(UiButton *button, const char *label, bool primary)
{
    if (button == NULL) {
        return;
    }

    button->bounds = (Rectangle){0, 0, 0, 0};
    button->label = label;
    button->enabled = true;
    button->primary = primary;
    button->hovered = false;
    button->pressed = false;
    button->hover_t = 0.0f;
}

void ui_button_set_bounds(UiButton *button, Rectangle bounds)
{
    if (button != NULL) {
        button->bounds = bounds;
    }
}

bool ui_button_update(UiButton *button, const UiInput *input,
                      bool accept_input, float dt)
{
    if (button == NULL) {
        return false;
    }

    const bool interactive = accept_input && button->enabled && input != NULL;
    const bool hover = interactive
        && CheckCollisionPointRec(input->mouse, button->bounds);

    button->hovered = hover;
    const float target = hover ? 1.0f : 0.0f;
    const float blend = (input != NULL && input->reduce_motion)
        ? 1.0f : ui_clamp01(dt * REVERSI_UI_MOTION_RATE);
    button->hover_t += (target - button->hover_t) * blend;

    if (!interactive) {
        button->pressed = false;
        return false;
    }

    bool clicked = false;
    if (hover && input->mouse_pressed) {
        button->pressed = true;
    }
    if (button->pressed && input->mouse_released) {
        clicked = hover;
        button->pressed = false;
    }
    if (!input->mouse_down) {
        button->pressed = false;
    }
    return clicked;
}

void ui_button_draw(const UiButton *button, const UiFonts *fonts,
                    bool focused)
{
    if (button == NULL) {
        return;
    }

    Rectangle rect = button->bounds;
    rect.y -= button->hover_t;
    if (button->pressed) {
        rect.y += 1.0f;
    }

    const float alpha = button->enabled
        ? 1.0f : REVERSI_UI_ALPHA_DISABLED;
    Color fill = REVERSI_UI_COLOR_SURFACE;
    Color label = REVERSI_UI_COLOR_TEXT;
    Color border = REVERSI_UI_COLOR_BORDER;

    if (button->primary) {
        fill = REVERSI_UI_COLOR_ACCENT;
        label = REVERSI_UI_COLOR_ACCENT_TEXT;
        border = (Color){0, 0, 0, 0};
        if (button->hovered) {
            fill = REVERSI_UI_COLOR_ACCENT_HOVER;
        }
        if (button->pressed) {
            fill = REVERSI_UI_COLOR_ACCENT_PRESSED;
        }
    } else {
        if (button->hovered) {
            fill = REVERSI_UI_COLOR_SURFACE_HOVER;
        }
        if (button->pressed) {
            fill = REVERSI_UI_COLOR_SURFACE_PRESSED;
        }
    }

    ui_draw_shadow(rect, REVERSI_UI_RADIUS_SM, button->pressed ? 2.0f : 6.0f,
                   button->primary ? 0.24f : 0.16f);
    ui_draw_rounded_rect(rect, REVERSI_UI_RADIUS_SM, ui_color_alpha(fill, alpha));
    if (border.a > 0) {
        ui_draw_rounded_outline(rect, REVERSI_UI_RADIUS_SM, 1.0f,
                                ui_color_alpha(border, alpha));
    }

    if (focused) {
        Rectangle focus = {
            .x = rect.x - 3.0f,
            .y = rect.y - 3.0f,
            .width = rect.width + 6.0f,
            .height = rect.height + 6.0f
        };
        ui_draw_rounded_outline(focus, REVERSI_UI_RADIUS_SM + 3.0f, 2.0f,
                                REVERSI_UI_COLOR_FOCUS);
    }

    ui_draw_text_aligned(fonts, UI_TEXT_SEMIBOLD, button->label, rect,
                         REVERSI_UI_FONT_BODY, ui_color_alpha(label, alpha),
                         UI_ALIGN_CENTER);
}
