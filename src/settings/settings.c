#include "settings/settings.h"

#include "storage/storage.h"
#include "storage/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_SETTINGS_MAX_BYTES 4096u

static const int SETTINGS_SPEEDS[REVERSI_SETTINGS_SPEED_COUNT] = {
    50, 75, 100, 150, 200
};

int settings_speed_next(int speed, int direction)
{
    int index = -1;
    for (int candidate = 0; candidate < REVERSI_SETTINGS_SPEED_COUNT;
         ++candidate) {
        if (SETTINGS_SPEEDS[candidate] == speed) {
            index = candidate;
            break;
        }
    }
    if (index < 0) {
        return 100;
    }
    index += direction < 0 ? -1 : 1;
    if (index < 0) {
        index = 0;
    } else if (index >= REVERSI_SETTINGS_SPEED_COUNT) {
        index = REVERSI_SETTINGS_SPEED_COUNT - 1;
    }
    return SETTINGS_SPEEDS[index];
}

void settings_defaults(Settings *settings)
{
    if (settings == NULL) {
        return;
    }
    *settings = (Settings){
        .volume = 70,
        .sfx = true,
        .animations = true,
        .animation_speed = 100,
        .legal_hints = true,
        .last_move_highlight = true,
        .default_difficulty = 1,
        .theme = 0
    };
}

static bool apply_setting(Settings *settings, const char *key,
                          const char *value)
{
    int number = 0;
    if (strcmp(key, "volume") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || number < 0 || number > 100) {
            return false;
        }
        settings->volume = number;
    } else if (strcmp(key, "sfx") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || (number != 0 && number != 1)) {
            return false;
        }
        settings->sfx = number == 1;
    } else if (strcmp(key, "animations") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || (number != 0 && number != 1)) {
            return false;
        }
        settings->animations = number == 1;
    } else if (strcmp(key, "animation_speed") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || number < REVERSI_SETTINGS_ANIMATION_MIN
            || number > REVERSI_SETTINGS_ANIMATION_MAX) {
            return false;
        }
        settings->animation_speed = number;
    } else if (strcmp(key, "legal_hints") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || (number != 0 && number != 1)) {
            return false;
        }
        settings->legal_hints = number == 1;
    } else if (strcmp(key, "last_move") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || (number != 0 && number != 1)) {
            return false;
        }
        settings->last_move_highlight = number == 1;
    } else if (strcmp(key, "default_difficulty") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || number < 0 || number > 2) {
            return false;
        }
        settings->default_difficulty = number;
    } else if (strcmp(key, "theme") == 0) {
        if (!storage_text_parse_int(value, -1000000, 1000000, &number)
            || (number != 0 && number != 1)) {
            return false;
        }
        settings->theme = number;
    }
    /* Unknown keys are ignored so future versions can add settings. */
    return true;
}

bool settings_load_path(const char *path, Settings *settings)
{
    if (settings == NULL) {
        return false;
    }
    Settings defaults;
    settings_defaults(&defaults);

    char *file = NULL;
    size_t size = 0;
    if (!storage_read(path, &file, &size, REVERSI_SETTINGS_MAX_BYTES)) {
        *settings = defaults;
        return false;
    }
    if (size == 0 || memchr(file, '\0', size) != NULL) {
        free(file);
        *settings = defaults;
        return false;
    }

    Settings parsed = defaults;
    bool ok = false;
    bool saw_header = false;
    char *cursor = file;
    char *line = NULL;
    while (storage_text_next_line(&cursor, &line)) {
        if (line[0] == '\0') {
            continue;
        }

        if (!saw_header) {
            if (strcmp(line, "REVERSI_SETTINGS 1") != 0) {
                goto done;
            }
            saw_header = true;
            continue;
        }

        char *space = strchr(line, ' ');
        if (space == NULL) {
            goto done;
        }
        *space = '\0';
        if (!apply_setting(&parsed, line, space + 1)) {
            goto done;
        }
    }

    ok = saw_header;
    if (ok) {
        *settings = parsed;
    }

done:
    free(file);
    if (!ok) {
        *settings = defaults;
    }
    return ok;
}

static bool settings_valid(const Settings *settings)
{
    return settings->volume >= 0 && settings->volume <= 100
        && settings->animation_speed >= REVERSI_SETTINGS_ANIMATION_MIN
        && settings->animation_speed <= REVERSI_SETTINGS_ANIMATION_MAX
        && settings->default_difficulty >= 0
        && settings->default_difficulty <= 2
        && (settings->theme == 0 || settings->theme == 1);
}

bool settings_save_path(const char *path, const Settings *settings)
{
    if (path == NULL || path[0] == '\0' || settings == NULL
        || !settings_valid(settings)) {
        return false;
    }

    char text[512];
    const int written = snprintf(
        text, sizeof(text),
        "REVERSI_SETTINGS 1\n"
        "volume %d\n"
        "sfx %d\n"
        "animations %d\n"
        "animation_speed %d\n"
        "legal_hints %d\n"
        "last_move %d\n"
        "default_difficulty %d\n"
        "theme %d\n",
        settings->volume,
        settings->sfx ? 1 : 0,
        settings->animations ? 1 : 0,
        settings->animation_speed,
        settings->legal_hints ? 1 : 0,
        settings->last_move_highlight ? 1 : 0,
        settings->default_difficulty,
        settings->theme);
    if (written <= 0 || (size_t)written >= sizeof(text)) {
        return false;
    }
    return storage_write_atomic(path, text, (size_t)written);
}

bool settings_load(Settings *settings)
{
    char path[1024];
    if (!storage_path(REVERSI_SETTINGS_FILE, path, sizeof(path))) {
        if (settings != NULL) {
            settings_defaults(settings);
        }
        return false;
    }
    return settings_load_path(path, settings);
}

bool settings_save(const Settings *settings)
{
    char path[1024];
    if (!storage_make_user_dir()
        || !storage_path(REVERSI_SETTINGS_FILE, path, sizeof(path))) {
        return false;
    }
    return settings_save_path(path, settings);
}
