#ifndef REVERSI_UI_FONTS_H
#define REVERSI_UI_FONTS_H

#include <raylib.h>

#include <stdbool.h>

typedef struct UiFonts {
    Font regular;
    Font semibold;
    bool custom;
} UiFonts;

/*
 * Loads the bundled Inter fonts relative to the executable directory.
 * Returns true when both custom fonts loaded; otherwise the default raylib
 * font is used and a warning is logged.
 */
bool ui_fonts_load(UiFonts *fonts);
void ui_fonts_unload(UiFonts *fonts);

Font ui_font_regular(const UiFonts *fonts);
Font ui_font_semibold(const UiFonts *fonts);

#endif
