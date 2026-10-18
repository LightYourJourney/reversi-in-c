#include "ui/fonts.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void build_asset_path(char *out, size_t capacity, const char *relative)
{
    const char *base = GetApplicationDirectory();
    const size_t length = (base != NULL) ? strlen(base) : 0;
    const bool has_separator = length > 0
        && (base[length - 1] == '/' || base[length - 1] == '\\');

    snprintf(out, capacity, "%s%s%s",
             (base != NULL) ? base : "",
             has_separator ? "" : "/",
             relative);
}

bool ui_fonts_load(UiFonts *fonts)
{
    if (fonts == NULL) {
        return false;
    }

    fonts->regular = GetFontDefault();
    fonts->semibold = GetFontDefault();
    fonts->custom = false;

    const Vector2 dpi = GetWindowScaleDPI();
    const float scale = dpi.x > 1.0f ? dpi.x : 1.0f;

    char path[1024];
    build_asset_path(path, sizeof(path), "assets/fonts/Inter-Regular.ttf");
    Font regular = LoadFontEx(path, (int)(32.0f * scale), NULL, 0);

    build_asset_path(path, sizeof(path), "assets/fonts/Inter-SemiBold.ttf");
    Font semibold = LoadFontEx(path, (int)(64.0f * scale), NULL, 0);

    const bool regular_ok = IsFontValid(regular) && regular.texture.id > 0;
    const bool semibold_ok = IsFontValid(semibold) && semibold.texture.id > 0;

    if (regular_ok && semibold_ok) {
        SetTextureFilter(regular.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(semibold.texture, TEXTURE_FILTER_BILINEAR);
        fonts->regular = regular;
        fonts->semibold = semibold;
        fonts->custom = true;
        TraceLog(LOG_INFO, "Reversi: loaded UI fonts (DPI scale %.1f)", scale);
        return true;
    }

    if (regular_ok) {
        UnloadFont(regular);
    }
    if (semibold_ok) {
        UnloadFont(semibold);
    }

    TraceLog(LOG_WARNING,
             "Reversi: bundled fonts unavailable, using raylib default font");
    return false;
}

void ui_fonts_unload(UiFonts *fonts)
{
    if (fonts == NULL || !fonts->custom) {
        return;
    }

    UnloadFont(fonts->regular);
    UnloadFont(fonts->semibold);
    fonts->regular = GetFontDefault();
    fonts->semibold = GetFontDefault();
    fonts->custom = false;
}

Font ui_font_regular(const UiFonts *fonts)
{
    if (fonts != NULL && fonts->custom) {
        return fonts->regular;
    }
    return GetFontDefault();
}

Font ui_font_semibold(const UiFonts *fonts)
{
    if (fonts != NULL && fonts->custom) {
        return fonts->semibold;
    }
    return GetFontDefault();
}
