#ifndef REVERSI_STORAGE_STATS_H
#define REVERSI_STORAGE_STATS_H

#include "core/match.h"

#include <stdbool.h>
#include <stddef.h>

#define REVERSI_STATS_DIFFICULTY_COUNT 3
#define REVERSI_STATS_FILE "stats.dat"

typedef struct DifficultyStats {
    int games;
    int wins;
    int draws;
    int losses;
} DifficultyStats;

typedef struct GameStats {
    DifficultyStats pve[REVERSI_STATS_DIFFICULTY_COUNT];
    int pvp_games;
    int pvp_draws;
} GameStats;

void stats_reset(GameStats *stats);

/* Aggregate values shared by the app and the statistics page. */
static inline int stats_total_games(const GameStats *stats)
{
    if (stats == NULL) {
        return 0;
    }
    int total = stats->pvp_games;
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        total += stats->pve[index].games;
    }
    return total;
}

static inline int stats_human_games(const GameStats *stats)
{
    if (stats == NULL) {
        return 0;
    }
    int games = 0;
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        games += stats->pve[index].games;
    }
    return games;
}

static inline int stats_human_wins(const GameStats *stats)
{
    if (stats == NULL) {
        return 0;
    }
    int wins = 0;
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        wins += stats->pve[index].wins;
    }
    return wins;
}

static inline int stats_human_draws(const GameStats *stats)
{
    if (stats == NULL) {
        return 0;
    }
    int draws = 0;
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        draws += stats->pve[index].draws;
    }
    return draws;
}

static inline int stats_human_losses(const GameStats *stats)
{
    if (stats == NULL) {
        return 0;
    }
    int losses = 0;
    for (int index = 0; index < REVERSI_STATS_DIFFICULTY_COUNT; ++index) {
        losses += stats->pve[index].losses;
    }
    return losses;
}

/*
 * Records one completed game. PVE wins/losses are from the human's
 * perspective; PVP tracks games and draws because both sides are human.
 * Ongoing results are ignored.
 */
void stats_record(GameStats *stats, GameMode mode, Stone human_color,
                  int difficulty, GameResult result);

/* Reverses one completed-game record (used when Undo reopens a result). */
void stats_unrecord(GameStats *stats, GameMode mode, Stone human_color,
                    int difficulty, GameResult result);

/* Missing or corrupt files reset to zero and return false. */
bool stats_load(GameStats *stats);
bool stats_save(const GameStats *stats);
bool stats_load_path(const char *path, GameStats *stats);
bool stats_save_path(const char *path, const GameStats *stats);

#endif
