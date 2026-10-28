#include "settings/settings.h"
#include "storage/storage.h"
#include "storage/text.h"
#include "test_harness.h"

#include <stdio.h>
#include <string.h>

#define TEST_SETTINGS_PATH "test-settings.dat"


static void test_text_helpers(TestStats *stats)
{
    char buffer[] = "alpha\r\n\nlast";
    char *cursor = buffer;
    char *line = NULL;
    REVERSI_TEST_ASSERT(stats, storage_text_next_line(&cursor, &line));
    REVERSI_TEST_ASSERT(stats, strcmp(line, "alpha") == 0);
    REVERSI_TEST_ASSERT(stats, storage_text_next_line(&cursor, &line));
    REVERSI_TEST_ASSERT(stats, line[0] == '\0');
    REVERSI_TEST_ASSERT(stats, storage_text_next_line(&cursor, &line));
    REVERSI_TEST_ASSERT(stats, strcmp(line, "last") == 0);
    REVERSI_TEST_ASSERT(stats, !storage_text_next_line(&cursor, &line));

    int value = 0;
    REVERSI_TEST_ASSERT(stats,
        storage_text_parse_int("42", 0, 100, &value) && value == 42);
    REVERSI_TEST_ASSERT(stats,
        storage_text_parse_int("100", 0, 100, &value) && value == 100);
    REVERSI_TEST_ASSERT(stats,
        !storage_text_parse_int("-1", 0, 100, &value));
    REVERSI_TEST_ASSERT(stats,
        !storage_text_parse_int("101", 0, 100, &value));
    REVERSI_TEST_ASSERT(stats,
        !storage_text_parse_int("12x", 0, 100, &value));
    REVERSI_TEST_ASSERT(stats,
        !storage_text_parse_int(NULL, 0, 100, &value));
}

static void test_speed_presets(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats, settings_speed_next(50, -1) == 50);
    REVERSI_TEST_ASSERT(stats, settings_speed_next(50, 1) == 75);
    REVERSI_TEST_ASSERT(stats, settings_speed_next(100, 1) == 150);
    REVERSI_TEST_ASSERT(stats, settings_speed_next(150, -1) == 100);
    REVERSI_TEST_ASSERT(stats, settings_speed_next(200, 1) == 200);
    REVERSI_TEST_ASSERT(stats, settings_speed_next(33, 1) == 100);
}

static void test_defaults(TestStats *stats)
{
    Settings settings;
    settings_defaults(&settings);
    REVERSI_TEST_ASSERT(stats, settings.volume == 70);
    REVERSI_TEST_ASSERT(stats, settings.sfx);
    REVERSI_TEST_ASSERT(stats, settings.animations);
    REVERSI_TEST_ASSERT(stats, settings.animation_speed == 100);
    REVERSI_TEST_ASSERT(stats, settings.legal_hints);
    REVERSI_TEST_ASSERT(stats, settings.last_move_highlight);
    REVERSI_TEST_ASSERT(stats, settings.default_difficulty == 1);
    REVERSI_TEST_ASSERT(stats, settings.theme == 0);

    settings.volume = 10;
    settings_defaults(NULL);
    settings_defaults(&settings);
    REVERSI_TEST_ASSERT(stats, settings.volume == 70);
}

static void test_round_trip(TestStats *stats)
{
    (void)storage_remove(TEST_SETTINGS_PATH);
    Settings saved;
    settings_defaults(&saved);
    saved.volume = 0;
    saved.sfx = false;
    saved.animations = false;
    saved.animation_speed = 200;
    saved.legal_hints = false;
    saved.last_move_highlight = false;
    saved.default_difficulty = 2;
    saved.theme = 1;

    REVERSI_TEST_ASSERT(stats,
        settings_save_path(TEST_SETTINGS_PATH, &saved));

    Settings loaded;
    settings_defaults(&loaded);
    REVERSI_TEST_ASSERT(stats,
        settings_load_path(TEST_SETTINGS_PATH, &loaded));
    REVERSI_TEST_ASSERT(stats, loaded.volume == 0);
    REVERSI_TEST_ASSERT(stats, !loaded.sfx);
    REVERSI_TEST_ASSERT(stats, !loaded.animations);
    REVERSI_TEST_ASSERT(stats, loaded.animation_speed == 200);
    REVERSI_TEST_ASSERT(stats, !loaded.legal_hints);
    REVERSI_TEST_ASSERT(stats, !loaded.last_move_highlight);
    REVERSI_TEST_ASSERT(stats, loaded.default_difficulty == 2);
    REVERSI_TEST_ASSERT(stats, loaded.theme == 1);

    (void)storage_remove(TEST_SETTINGS_PATH);

    Settings invalid;
    settings_defaults(&invalid);
    invalid.volume = 101;
    REVERSI_TEST_ASSERT(stats,
        !settings_save_path(TEST_SETTINGS_PATH, &invalid));
    invalid = saved;
    invalid.animation_speed = 33;
    REVERSI_TEST_ASSERT(stats,
        !settings_save_path(TEST_SETTINGS_PATH, &invalid));
    REVERSI_TEST_ASSERT(stats, !storage_exists(TEST_SETTINGS_PATH));
}

static void test_invalid_files(TestStats *stats)
{
    const char *path = "test-settings-invalid.dat";
    (void)storage_remove(path);
    Settings settings;
    settings_defaults(&settings);
    settings.volume = 12;
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));
    REVERSI_TEST_ASSERT(stats, settings.volume == 70);

    const char *bad_header = "REVERSI_SETTINGS 2\nvolume 30\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_header, strlen(bad_header)));
    settings.volume = 12;
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));
    REVERSI_TEST_ASSERT(stats, settings.volume == 70);

    const char *bad_volume =
        "REVERSI_SETTINGS 1\nvolume 101\nsfx 1\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_volume, strlen(bad_volume)));
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));
    REVERSI_TEST_ASSERT(stats, settings.volume == 70);

    const char *bad_speed =
        "REVERSI_SETTINGS 1\nanimation_speed 33\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_speed, strlen(bad_speed)));
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));

    const char *bad_bool = "REVERSI_SETTINGS 1\nsfx 2\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_bool, strlen(bad_bool)));
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));

    const char *bad_line = "REVERSI_SETTINGS 1\nvolume\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_line, strlen(bad_line)));
    REVERSI_TEST_ASSERT(stats, !settings_load_path(path, &settings));

    const char *unknown =
        "REVERSI_SETTINGS 1\nvolume 25\nfuture_option 9\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, unknown, strlen(unknown)));
    REVERSI_TEST_ASSERT(stats, settings_load_path(path, &settings));
    REVERSI_TEST_ASSERT(stats, settings.volume == 25);

    (void)storage_remove(path);
}

static void test_v101_fixture(TestStats *stats)
{
    /* Verbatim settings_save_path() output from tag v1.0.1. */
    static const char LEGACY[] =
        "REVERSI_SETTINGS 1\n"
        "volume 33\n"
        "sfx 0\n"
        "animations 1\n"
        "animation_speed 150\n"
        "legal_hints 0\n"
        "last_move 1\n"
        "default_difficulty 2\n"
        "theme 1\n";
    const char *path = "test-settings-v101.dat";
    (void)storage_remove(path);
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, LEGACY, strlen(LEGACY)));
    Settings loaded;
    settings_defaults(&loaded);
    REVERSI_TEST_ASSERT(stats, settings_load_path(path, &loaded));
    REVERSI_TEST_ASSERT(stats, loaded.volume == 33);
    REVERSI_TEST_ASSERT(stats, !loaded.sfx);
    REVERSI_TEST_ASSERT(stats, loaded.animations);
    REVERSI_TEST_ASSERT(stats, loaded.animation_speed == 150);
    REVERSI_TEST_ASSERT(stats, !loaded.legal_hints);
    REVERSI_TEST_ASSERT(stats, loaded.last_move_highlight);
    REVERSI_TEST_ASSERT(stats, loaded.default_difficulty == 2);
    REVERSI_TEST_ASSERT(stats, loaded.theme == 1);
    (void)storage_remove(path);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_text_helpers);
    REVERSI_RUN_TEST(&stats, test_speed_presets);
    REVERSI_RUN_TEST(&stats, test_defaults);
    REVERSI_RUN_TEST(&stats, test_round_trip);
    REVERSI_RUN_TEST(&stats, test_invalid_files);
    REVERSI_RUN_TEST(&stats, test_v101_fixture);

    return reversi_test_summary(&stats, "storage.settings");
}
