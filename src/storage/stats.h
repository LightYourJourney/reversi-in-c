#ifndef REVERSI_STORAGE_STATS_H
#define REVERSI_STORAGE_STATS_H

#include "core/match.h"

#include <stdbool.h>

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

/* Aggregate values used by the statistics page. */
int stats_total_games(const GameStats *stats);
int stats_human_games(const GameStats *stats);
int stats_human_wins(const GameStats *stats);
int stats_human_draws(const GameStats *stats);
int stats_human_losses(const GameStats *stats);

/*
 * Records one completed game. PVE wins/losses are from the human's
 * perspective; PVP tracks games and draws because both sides are human.
 * Ongoing results are ignored.
 */
void stats_record(GameStats *stats, GameMode mode, Stone human_color,
                  int difficulty, GameResult result);

/* Missing or corrupt files reset to zero and return false. */
bool stats_load(GameStats *stats);
bool stats_save(const GameStats *stats);
bool stats_load_path(const char *path, GameStats *stats);
bool stats_save_path(const char *path, const GameStats *stats);

#endif
