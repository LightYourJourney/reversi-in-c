#ifndef REVERSI_UI_THEME_H
#define REVERSI_UI_THEME_H

#include <raylib.h>

/* Spacing scale (logical pixels). */
#define UI_SPACE_XS 4.0f
#define UI_SPACE_SM 8.0f
#define UI_SPACE_MD 16.0f
#define UI_SPACE_LG 24.0f
#define UI_SPACE_XL 32.0f
#define UI_SPACE_2XL 48.0f

/* Corner radii. */
#define UI_RADIUS_SM 10.0f
#define UI_RADIUS_MD 16.0f
#define UI_RADIUS_LG 24.0f

/* Type scale (logical pixels). */
#define UI_FONT_DISPLAY 64
#define UI_FONT_TITLE 40
#define UI_FONT_HEADING 28
#define UI_FONT_BODY 19
#define UI_FONT_SMALL 15
#define UI_FONT_TINY 13

/* Motion. */
#define UI_DUR_FAST 0.12f
#define UI_DUR_NORMAL 0.22f
#define UI_DUR_TRANSITION 0.36f
#define UI_DUR_PLACE 0.20f
#define UI_DUR_FLIP 0.26f

/* Window. */
#define UI_MIN_WINDOW_WIDTH 760
#define UI_MIN_WINDOW_HEIGHT 600

/* Surfaces and text. */
#define UI_COLOR_BG_TOP ((Color){12, 16, 21, 255})
#define UI_COLOR_BG_BOTTOM ((Color){20, 26, 33, 255})
#define UI_COLOR_SURFACE ((Color){25, 31, 39, 255})
#define UI_COLOR_SURFACE_ALT ((Color){30, 37, 46, 255})
#define UI_COLOR_SURFACE_HOVER ((Color){35, 43, 53, 255})
#define UI_COLOR_SURFACE_PRESSED ((Color){21, 26, 33, 255})
#define UI_COLOR_BORDER ((Color){255, 255, 255, 24})
#define UI_COLOR_BORDER_STRONG ((Color){255, 255, 255, 42})
#define UI_COLOR_TEXT ((Color){235, 239, 244, 255})
#define UI_COLOR_TEXT_MUTED ((Color){150, 161, 175, 255})
#define UI_COLOR_TEXT_FAINT ((Color){105, 116, 130, 255})

/* Accent. */
#define UI_COLOR_ACCENT ((Color){88, 191, 155, 255})
#define UI_COLOR_ACCENT_HOVER ((Color){107, 209, 173, 255})
#define UI_COLOR_ACCENT_PRESSED ((Color){72, 167, 134, 255})
#define UI_COLOR_ACCENT_SOFT ((Color){88, 191, 155, 36})
#define UI_COLOR_ACCENT_TEXT ((Color){10, 20, 16, 255})
#define UI_COLOR_FOCUS ((Color){140, 225, 193, 255})

/* Board and pieces. */
#define UI_COLOR_BOARD ((Color){30, 73, 56, 255})
#define UI_COLOR_BOARD_DARK ((Color){22, 55, 42, 255})
#define UI_COLOR_BOARD_EDGE ((Color){15, 40, 31, 255})
#define UI_COLOR_GRID ((Color){255, 255, 255, 18})
#define UI_COLOR_BLACK_DISC ((Color){20, 23, 27, 255})
#define UI_COLOR_BLACK_RIM ((Color){6, 8, 10, 255})
#define UI_COLOR_WHITE_DISC ((Color){238, 235, 226, 255})
#define UI_COLOR_WHITE_RIM ((Color){196, 192, 182, 255})

/* Overlays. */
#define UI_COLOR_OVERLAY ((Color){5, 7, 10, 205})
#define UI_COLOR_DANGER ((Color){214, 112, 102, 255})

#endif
