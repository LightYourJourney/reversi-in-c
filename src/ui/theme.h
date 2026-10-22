#ifndef REVERSI_UI_THEME_H
#define REVERSI_UI_THEME_H

#include <raylib.h>

/* Spacing scale (logical pixels). */
#define REVERSI_UI_SPACE_LG 24.0f
#define REVERSI_UI_SPACE_XL 32.0f

/* Corner radii. */
#define REVERSI_UI_RADIUS_SM 10.0f
#define REVERSI_UI_RADIUS_MD 16.0f
#define REVERSI_UI_RADIUS_LG 24.0f

/* Type scale (logical pixels). */
#define REVERSI_UI_FONT_DISPLAY 64
#define REVERSI_UI_FONT_TITLE 40
#define REVERSI_UI_FONT_HEADING 28
#define REVERSI_UI_FONT_BODY 19
#define REVERSI_UI_FONT_SMALL 15
#define REVERSI_UI_FONT_TINY 13

/* Motion. */
#define REVERSI_UI_DUR_TRANSITION 0.36f
#define REVERSI_UI_DUR_PLACE 0.20f
#define REVERSI_UI_DUR_FLIP 0.26f
#define REVERSI_UI_DUR_REJECT 0.24f
#define REVERSI_UI_DUR_MODAL 0.16f
#define REVERSI_UI_DUR_RESULT 0.42f
#define REVERSI_UI_DUR_PASS_NOTICE 0.90f
#define REVERSI_UI_DUR_GAME_OVER 0.75f
#define REVERSI_UI_DUR_GAME_OVER_FAST 0.20f
#define REVERSI_UI_DUR_NOTICE 3.2f
#define REVERSI_UI_DUR_NOTICE_FADE 0.4f
#define REVERSI_UI_MOTION_RATE 14.0f

/* Window. */
#define REVERSI_UI_MIN_WINDOW_WIDTH 760
#define REVERSI_UI_MIN_WINDOW_HEIGHT 600
#define REVERSI_UI_NARROW_ACTION_WIDTH 520.0f

/* Runtime theme palette. All UI colors go through the active palette so the
   theme can change without restarting. */
typedef enum UiThemeKind {
    REVERSI_UI_THEME_DARK = 0,
    REVERSI_UI_THEME_LIGHT
} UiThemeKind;

typedef struct UiTheme {
    Color bg_top;
    Color bg_bottom;
    Color surface;
    Color surface_alt;
    Color surface_hover;
    Color surface_pressed;
    Color border;
    Color border_strong;
    Color text;
    Color text_muted;
    Color text_faint;
    Color accent;
    Color accent_hover;
    Color accent_pressed;
    Color accent_soft;
    Color accent_text;
    Color focus;
    Color hint;
    Color board;
    Color board_edge;
    Color grid;
    Color black_disc;
    Color black_rim;
    Color white_disc;
    Color white_rim;
    Color shadow;
    Color highlight;
    Color scrim;
    Color overlay;
    Color danger;
} UiTheme;

extern const UiTheme *ui_theme;

void ui_theme_set(UiThemeKind kind);
UiThemeKind ui_theme_current(void);
const char *ui_theme_name(UiThemeKind kind);

#define REVERSI_UI_COLOR_BG_TOP (ui_theme->bg_top)
#define REVERSI_UI_COLOR_BG_BOTTOM (ui_theme->bg_bottom)
#define REVERSI_UI_COLOR_SURFACE (ui_theme->surface)
#define REVERSI_UI_COLOR_SURFACE_ALT (ui_theme->surface_alt)
#define REVERSI_UI_COLOR_SURFACE_HOVER (ui_theme->surface_hover)
#define REVERSI_UI_COLOR_SURFACE_PRESSED (ui_theme->surface_pressed)
#define REVERSI_UI_COLOR_BORDER (ui_theme->border)
#define REVERSI_UI_COLOR_BORDER_STRONG (ui_theme->border_strong)
#define REVERSI_UI_COLOR_TEXT (ui_theme->text)
#define REVERSI_UI_COLOR_TEXT_MUTED (ui_theme->text_muted)
#define REVERSI_UI_COLOR_TEXT_FAINT (ui_theme->text_faint)

#define REVERSI_UI_COLOR_ACCENT (ui_theme->accent)
#define REVERSI_UI_COLOR_ACCENT_HOVER (ui_theme->accent_hover)
#define REVERSI_UI_COLOR_ACCENT_PRESSED (ui_theme->accent_pressed)
#define REVERSI_UI_COLOR_ACCENT_SOFT (ui_theme->accent_soft)
#define REVERSI_UI_COLOR_ACCENT_TEXT (ui_theme->accent_text)
#define REVERSI_UI_COLOR_FOCUS (ui_theme->focus)
#define REVERSI_UI_COLOR_HINT (ui_theme->hint)

#define REVERSI_UI_COLOR_BOARD (ui_theme->board)
#define REVERSI_UI_COLOR_BOARD_EDGE (ui_theme->board_edge)
#define REVERSI_UI_COLOR_GRID (ui_theme->grid)
#define REVERSI_UI_COLOR_BLACK_DISC (ui_theme->black_disc)
#define REVERSI_UI_COLOR_BLACK_RIM (ui_theme->black_rim)
#define REVERSI_UI_COLOR_WHITE_DISC (ui_theme->white_disc)
#define REVERSI_UI_COLOR_WHITE_RIM (ui_theme->white_rim)

#define REVERSI_UI_COLOR_SHADOW (ui_theme->shadow)
#define REVERSI_UI_COLOR_HIGHLIGHT (ui_theme->highlight)
#define REVERSI_UI_COLOR_SCRIM (ui_theme->scrim)
#define REVERSI_UI_COLOR_OVERLAY (ui_theme->overlay)
#define REVERSI_UI_COLOR_DANGER (ui_theme->danger)

/* Alpha values for recurring interaction states. */
#define REVERSI_UI_ALPHA_DISABLED 0.38f
#define REVERSI_UI_ALPHA_LAST_MOVE 0.16f
#define REVERSI_UI_ALPHA_GHOST 0.20f
#define REVERSI_UI_ALPHA_HINT 0.62f
#define REVERSI_UI_ALPHA_CURSOR_LEGAL 0.75f
#define REVERSI_UI_ALPHA_CURSOR_IDLE 0.28f

#endif
