#include "ui/theme.h"

static const UiTheme THEME_DARK = {
    .bg_top = {12, 16, 21, 255},
    .bg_bottom = {20, 26, 33, 255},
    .surface = {25, 31, 39, 255},
    .surface_alt = {30, 37, 46, 255},
    .surface_hover = {35, 43, 53, 255},
    .surface_pressed = {21, 26, 33, 255},
    .border = {255, 255, 255, 24},
    .border_strong = {255, 255, 255, 42},
    .text = {235, 239, 244, 255},
    .text_muted = {150, 161, 175, 255},
    .text_faint = {105, 116, 130, 255},
    .accent = {88, 191, 155, 255},
    .accent_hover = {107, 209, 173, 255},
    .accent_pressed = {72, 167, 134, 255},
    .accent_soft = {88, 191, 155, 36},
    .accent_text = {10, 20, 16, 255},
    .focus = {140, 225, 193, 255},
    .hint = {88, 191, 155, 255},
    .board = {30, 73, 56, 255},
    .board_edge = {15, 40, 31, 255},
    .grid = {255, 255, 255, 18},
    .black_disc = {20, 23, 27, 255},
    .black_rim = {6, 8, 10, 255},
    .white_disc = {238, 235, 226, 255},
    .white_rim = {196, 192, 182, 255},
    .shadow = {0, 0, 0, 255},
    .highlight = {255, 255, 255, 255},
    .scrim = {0, 0, 0, 255},
    .overlay = {5, 7, 10, 205},
    .danger = {214, 112, 102, 255}
};

static const UiTheme THEME_LIGHT = {
    .bg_top = {239, 243, 247, 255},
    .bg_bottom = {226, 233, 240, 255},
    .surface = {255, 255, 255, 255},
    .surface_alt = {247, 249, 252, 255},
    .surface_hover = {236, 242, 247, 255},
    .surface_pressed = {219, 227, 235, 255},
    .border = {15, 23, 32, 30},
    .border_strong = {15, 23, 32, 55},
    .text = {22, 29, 37, 255},
    .text_muted = {82, 94, 108, 255},
    .text_faint = {104, 116, 130, 255},
    .accent = {34, 126, 100, 255},
    .accent_hover = {45, 145, 117, 255},
    .accent_pressed = {26, 106, 83, 255},
    .accent_soft = {34, 126, 100, 28},
    .accent_text = {255, 255, 255, 255},
    .focus = {22, 96, 76, 255},
    .hint = {236, 246, 241, 255},
    .board = {48, 108, 82, 255},
    .board_edge = {27, 68, 52, 255},
    .grid = {0, 0, 0, 22},
    .black_disc = {22, 26, 31, 255},
    .black_rim = {6, 8, 10, 255},
    .white_disc = {252, 250, 245, 255},
    .white_rim = {178, 174, 164, 255},
    .shadow = {0, 0, 0, 255},
    .highlight = {255, 255, 255, 255},
    .scrim = {0, 0, 0, 255},
    .overlay = {20, 25, 31, 205},
    .danger = {185, 66, 56, 255}
};

const UiTheme *ui_theme = &THEME_DARK;

void ui_theme_set(UiThemeKind kind)
{
    switch (kind) {
    case REVERSI_UI_THEME_DARK:
        ui_theme = &THEME_DARK;
        break;
    case REVERSI_UI_THEME_LIGHT:
        ui_theme = &THEME_LIGHT;
        break;
    default:
        break;
    }
}

UiThemeKind ui_theme_current(void)
{
    return ui_theme == &THEME_LIGHT
        ? REVERSI_UI_THEME_LIGHT : REVERSI_UI_THEME_DARK;
}

const char *ui_theme_name(UiThemeKind kind)
{
    return kind == REVERSI_UI_THEME_LIGHT ? "Light" : "Dark";
}
