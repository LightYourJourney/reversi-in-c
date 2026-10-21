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

static bool font_is_custom(Font font)
{
    return IsFontValid(font) && font.texture.id > 0
        && font.texture.id != GetFontDefault().texture.id;
}

static void unload_if_not_default(Font font)
{
    if (font.texture.id > 0
        && font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(font);
    }
}

static Font load_ui_font(const char *relative, int size)
{
    char path[1024];

#if defined(__APPLE__)
    /* A macOS .app keeps data files in Contents/Resources. */
    char bundle_relative[1024];
    const int written = snprintf(bundle_relative, sizeof(bundle_relative),
                                 "../Resources/%s", relative);
    if (written > 0 && (size_t)written < sizeof(bundle_relative)) {
        build_asset_path(path, sizeof(path), bundle_relative);
        if (FileExists(path)) {
            Font bundled = LoadFontEx(path, size, NULL, 0);
            if (font_is_custom(bundled)) {
                return bundled;
            }
            unload_if_not_default(bundled);
        }
    }
#endif

    build_asset_path(path, sizeof(path), relative);
    Font font = LoadFontEx(path, size, NULL, 0);
    if (font_is_custom(font)) {
        return font;
    }
    unload_if_not_default(font);
    return GetFontDefault();
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

    Font regular = load_ui_font("assets/fonts/Inter-Regular.ttf",
                                (int)(32.0f * scale));
    Font semibold = load_ui_font("assets/fonts/Inter-SemiBold.ttf",
                                 (int)(64.0f * scale));

    const bool regular_ok = font_is_custom(regular);
    const bool semibold_ok = font_is_custom(semibold);

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
