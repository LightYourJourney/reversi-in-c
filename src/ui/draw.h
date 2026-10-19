#ifndef REVERSI_UI_DRAW_H
#define REVERSI_UI_DRAW_H

#include "core/board.h"
#include "core/game.h"
#include "ui/fonts.h"

#include <raylib.h>
#include <stdbool.h>

typedef enum UiAlign {
    UI_ALIGN_LEFT = 0,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT
} UiAlign;

typedef enum UiTextWeight {
    UI_TEXT_REGULAR = 0,
    UI_TEXT_SEMIBOLD
} UiTextWeight;

typedef struct UiInput {
    Vector2 mouse;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;
    bool escape_pressed;
    bool enter_pressed;
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool restart_pressed;
    bool menu_pressed;
    bool pause_pressed;
    bool undo_pressed;
    bool redo_pressed;
    bool history_pressed;
    float wheel;
    bool reduce_motion;
} UiInput;

typedef struct UiButton {
    Rectangle bounds;
    const char *label;
    bool enabled;
    bool primary;
    bool hovered;
    bool pressed;
    float hover_t;
} UiButton;

void ui_button_init(UiButton *button, const char *label, bool primary);
void ui_button_set_bounds(UiButton *button, Rectangle bounds);
bool ui_button_update(UiButton *button, const UiInput *input,
                      bool accept_input, float dt);
void ui_button_draw(const UiButton *button, const UiFonts *fonts,
                    bool focused);

void ui_draw_background(void);
void ui_draw_shadow(Rectangle rect, float radius, float offset, float alpha);
void ui_draw_panel(Rectangle rect, float radius, Color fill, Color border);
void ui_draw_rounded_rect(Rectangle rect, float radius, Color color);
void ui_draw_rounded_outline(Rectangle rect, float radius, float thickness,
                             Color color);
void ui_draw_text_aligned(const UiFonts *fonts, UiTextWeight weight,
                          const char *text, Rectangle box, float size,
                          Color color, UiAlign align);
void ui_draw_text_clipped(const UiFonts *fonts, UiTextWeight weight,
                          const char *text, Rectangle box, float size,
                          Color color, UiAlign align);

void ui_draw_disc(Vector2 center, float radius, Stone stone,
                  float alpha, float scale_x, float scale_y);
void ui_draw_ring(Vector2 center, float radius, float thickness, Color color);

float ui_clamp01(float value);
float ui_ease_out_back(float value);
Color ui_color_alpha(Color color, float alpha);
void ui_focus_update(int *focus, bool *visible, int count,
                     const UiInput *input);

/* Display name for a side ("Black"/"White"); EMPTY maps to "None". */
const char *ui_player_name(Stone stone);

/* Win/draw title, or NULL while the game is still ongoing. */
const char *ui_result_title(GameResult result);

/*
 * Advances a focus index by direction (+1/-1), wrapping within [0, count)
 * and skipping disabled_index. A negative disabled_index disables skipping.
 */
int ui_focus_step(int focus, int direction, int count, int disabled_index);

#endif
