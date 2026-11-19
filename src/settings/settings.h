#ifndef REVERSI_SETTINGS_SETTINGS_H
#define REVERSI_SETTINGS_SETTINGS_H

#include <stdbool.h>

#define REVERSI_SETTINGS_FILE "settings.dat"
#define REVERSI_SETTINGS_ANIMATION_MIN 50
#define REVERSI_SETTINGS_ANIMATION_MAX 200
#define REVERSI_SETTINGS_SPEED_COUNT 5
#define REVERSI_SETTINGS_VOLUME_MAX 100
#define REVERSI_SETTINGS_VOLUME_STEP 5
#define REVERSI_SETTINGS_DIFFICULTY_COUNT 3
#define REVERSI_SETTINGS_THEME_COUNT 2

typedef struct Settings {
    int volume;              /* 0..100 */
    bool sfx;
    bool animations;
    int animation_speed;     /* percent, 50..200 */
    bool legal_hints;
    bool last_move_highlight;
    int default_difficulty;  /* 0 = Easy, 1 = Medium, 2 = Hard */
    int theme;               /* 0 = Dark, 1 = Light */
} Settings;

void settings_defaults(Settings *settings);

/* Next preset speed in direction (-1 slower, +1 faster), clamped. */
/* The value one step up (direction > 0) or down from the current one.
 * Speed and volume stop at their ends; difficulty and theme wrap around. */
int settings_speed_next(int speed, int direction);
int settings_volume_next(int volume, int direction);
int settings_difficulty_next(int difficulty, int direction);
int settings_theme_next(int theme, int direction);

/*
 * Loads persisted settings from the user data directory. Missing, corrupt,
 * or invalid files leave defaults in place and return false; the program can
 * always continue.
 */
bool settings_load(Settings *settings);
bool settings_save(const Settings *settings);

/* Explicit-path variants used by tests. */
bool settings_load_path(const char *path, Settings *settings);
bool settings_save_path(const char *path, const Settings *settings);

#endif
