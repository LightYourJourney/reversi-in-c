#include "storage/stats.h"

#include "storage/storage.h"
#include "storage/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_STATS_MAX_BYTES 4096u

static const char *const DIFFICULTY_KEYS[REVERSI_STATS_DIFFICULTY_COUNT] = {
    "easy", "medium", "hard"
};

void stats_reset(GameStats *stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

void stats_record(GameStats *stats, GameMode mode, Stone human_color,
                  int difficulty, GameResult result)
{
    if (stats == NULL || result == GAME_RESULT_ONGOING) {
        return;
    }

    if (mode == GAME_MODE_PVP) {
        ++stats->pvp_games;
        if (result == GAME_RESULT_DRAW) {
            ++stats->pvp_draws;
        }
        return;
    }
    if (mode != GAME_MODE_PVE || difficulty < 0
        || difficulty >= REVERSI_STATS_DIFFICULTY_COUNT
        || (human_color != STONE_BLACK && human_color != STONE_WHITE)) {
        return;
    }

    DifficultyStats *record = &stats->pve[difficulty];
    ++record->games;
    if (result == GAME_RESULT_DRAW) {
        ++record->draws;
        return;
    }
    const GameResult human_win = human_color == STONE_BLACK
        ? GAME_RESULT_BLACK_WIN : GAME_RESULT_WHITE_WIN;
    if (result == human_win) {
        ++record->wins;
    } else {
        ++record->losses;
    }
}

void stats_unrecord(GameStats *stats, GameMode mode, Stone human_color,
                    int difficulty, GameResult result)
{
    if (stats == NULL || result == GAME_RESULT_ONGOING) {
        return;
    }

    if (mode == GAME_MODE_PVP) {
        if (stats->pvp_games <= 0
            || (result == GAME_RESULT_DRAW && stats->pvp_draws <= 0)) {
            return;
        }
        --stats->pvp_games;
        if (result == GAME_RESULT_DRAW) {
            --stats->pvp_draws;
        }
        return;
    }
    if (mode != GAME_MODE_PVE || difficulty < 0
        || difficulty >= REVERSI_STATS_DIFFICULTY_COUNT
        || (human_color != STONE_BLACK && human_color != STONE_WHITE)) {
        return;
    }

    DifficultyStats *record = &stats->pve[difficulty];
    const GameResult human_win = human_color == STONE_BLACK
        ? GAME_RESULT_BLACK_WIN : GAME_RESULT_WHITE_WIN;
    if (record->games <= 0
        || (result == GAME_RESULT_DRAW && record->draws <= 0)
        || (result != GAME_RESULT_DRAW && result == human_win
            && record->wins <= 0)
        || (result != GAME_RESULT_DRAW && result != human_win
            && record->losses <= 0)) {
        return;
    }
    --record->games;
    if (result == GAME_RESULT_DRAW) {
        --record->draws;
    } else if (result == human_win) {
        --record->wins;
    } else {
        --record->losses;
    }
}

static bool apply_field(GameStats *stats, const char *key, const char *value)
{
    if (strcmp(key, "pvp_games") == 0 || strcmp(key, "pvp_draws") == 0) {
        int number = 0;
        if (!storage_text_parse_int(value, 0, 100000000, &number)) {
            return false;
        }
        if (strcmp(key, "pvp_games") == 0) {
            stats->pvp_games = number;
        } else {
            stats->pvp_draws = number;
        }
        return true;
    }

    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        const size_t key_length = strlen(DIFFICULTY_KEYS[index]);
        if (strncmp(key, DIFFICULTY_KEYS[index], key_length) != 0
            || key[key_length] != '_') {
            continue;
        }
        const char *field = key + key_length + 1;
        int number = 0;
        if (!storage_text_parse_int(value, 0, 100000000, &number)) {
            return false;
        }
        DifficultyStats *record = &stats->pve[index];
        if (strcmp(field, "games") == 0) {
            record->games = number;
        } else if (strcmp(field, "wins") == 0) {
            record->wins = number;
        } else if (strcmp(field, "draws") == 0) {
            record->draws = number;
        } else if (strcmp(field, "losses") == 0) {
            record->losses = number;
        }
        return true;
    }
    /* Unknown keys are ignored for forward compatibility. */
    return true;
}

static bool stats_valid(const GameStats *stats)
{
    if (stats->pvp_games < 0 || stats->pvp_draws < 0
        || stats->pvp_draws > stats->pvp_games) {
        return false;
    }
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        const DifficultyStats *record = &stats->pve[index];
        if (record->games < 0 || record->wins < 0 || record->draws < 0
            || record->losses < 0
            || record->wins + record->draws + record->losses
                != record->games) {
            return false;
        }
    }
    return true;
}

bool stats_load_path(const char *path, GameStats *stats)
{
    if (stats == NULL) {
        return false;
    }
    stats_reset(stats);

    char *file = NULL;
    size_t size = 0;
    if (!storage_read(path, &file, &size, REVERSI_STATS_MAX_BYTES)) {
        return false;
    }
    if (size == 0 || memchr(file, '\0', size) != NULL) {
        free(file);
        return false;
    }

    GameStats parsed;
    stats_reset(&parsed);
    bool saw_header = false;
    bool ok = true;
    char *cursor = file;
    char *line = NULL;
    while (storage_text_next_line(&cursor, &line)) {
        if (line[0] == '\0') {
            continue;
        }
        if (!saw_header) {
            if (strcmp(line, "REVERSI_STATS 1") != 0) {
                ok = false;
                break;
            }
            saw_header = true;
            continue;
        }
        char *space = strchr(line, ' ');
        if (space == NULL) {
            ok = false;
            break;
        }
        *space = '\0';
        if (!apply_field(&parsed, line, space + 1)) {
            ok = false;
            break;
        }
    }

    free(file);
    if (!ok || !saw_header || !stats_valid(&parsed)) {
        stats_reset(stats);
        return false;
    }
    *stats = parsed;
    return true;
}

bool stats_save_path(const char *path, const GameStats *stats)
{
    if (path == NULL || path[0] == '\0' || stats == NULL
        || !stats_valid(stats)) {
        return false;
    }

    char text[1024];
    int written = snprintf(
        text, sizeof(text),
        "REVERSI_STATS 1\npvp_games %d\npvp_draws %d\n",
        stats->pvp_games, stats->pvp_draws);
    if (written <= 0 || (size_t)written >= sizeof(text)) {
        return false;
    }
    size_t length = (size_t)written;

    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        const DifficultyStats *record = &stats->pve[index];
        written = snprintf(
            text + length, sizeof(text) - length,
            "%s_games %d\n%s_wins %d\n%s_draws %d\n%s_losses %d\n",
            DIFFICULTY_KEYS[index], record->games,
            DIFFICULTY_KEYS[index], record->wins,
            DIFFICULTY_KEYS[index], record->draws,
            DIFFICULTY_KEYS[index], record->losses);
        if (written <= 0 || (size_t)written >= sizeof(text) - length) {
            return false;
        }
        length += (size_t)written;
    }

    return storage_write_atomic(path, text, length);
}

bool stats_load(GameStats *stats)
{
    char path[1024];
    if (!storage_path(REVERSI_STATS_FILE, path, sizeof(path))) {
        stats_reset(stats);
        return false;
    }
    return stats_load_path(path, stats);
}

bool stats_save(const GameStats *stats)
{
    char path[1024];
    if (!storage_make_user_dir()
        || !storage_path(REVERSI_STATS_FILE, path, sizeof(path))) {
        return false;
    }
    return stats_save_path(path, stats);
}
