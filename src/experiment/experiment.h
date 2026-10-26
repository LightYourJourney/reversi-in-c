#ifndef REVERSI_EXPERIMENT_EXPERIMENT_H
#define REVERSI_EXPERIMENT_EXPERIMENT_H

#include "ai/ai.h"
#include "core/game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REVERSI_EXPERIMENT_MAX_GAMES 512
#define REVERSI_EXPERIMENT_MAX_OPENINGS 64
#define REVERSI_EXPERIMENT_MAX_PLIES 128
#define REVERSI_EXPERIMENT_MAX_ENGINES 2
#define REVERSI_EXPERIMENT_CONFIG_VERSION 1

typedef enum ExperimentEngineKind {
    EXPERIMENT_ENGINE_RANDOM = 0,
    EXPERIMENT_ENGINE_GREEDY,
    EXPERIMENT_ENGINE_AI,
    EXPERIMENT_ENGINE_SEARCH
} ExperimentEngineKind;

typedef struct ExperimentEngine {
    char name[32];
    ExperimentEngineKind kind;
    AiDifficulty difficulty;
    int max_depth;
    int time_budget_ms;
    uint64_t max_nodes;
    bool use_tt;
    bool use_endgame;
    unsigned seed;
} ExperimentEngine;

typedef struct ExperimentConfig {
    char name[48];
    unsigned seed;
    int games;
    int openings;
    int opening_plies;
    int max_plies;
    ExperimentEngine engines[REVERSI_EXPERIMENT_MAX_ENGINES];
} ExperimentConfig;

typedef struct ExperimentMoveSample {
    int game;
    int ply;
    int engine;
    Stone color;
    Move move;
    int depth;
    int score;
    uint64_t nodes;
    uint64_t cutoffs;
    uint64_t tt_hits;
    int elapsed_ms;
    bool cancelled;
    bool timed_out;
    bool node_limit;
} ExperimentMoveSample;

typedef struct ExperimentGame {
    int index;
    int opening;
    Stone a_color;
    int plies;
    int passes;
    GameResult result;
    int black_discs;
    int white_discs;
    bool illegal;
    bool timed_out;
    bool node_limit;
    bool aborted;
} ExperimentGame;

typedef struct ExperimentEngineStats {
    int moves;
    uint64_t nodes;
    uint64_t cutoffs;
    uint64_t tt_hits;
    uint64_t elapsed_ms;
    int avg_ms;
    int avg_depth;
    int max_depth;
    int p50_ms;
    int p95_ms;
    int max_ms;
    double nps;
    int timeouts;
    int node_limits;
    int cancellations;
} ExperimentEngineStats;

typedef struct ExperimentSummary {
    int games;
    int completed;
    /* Completed games that belong to a fully completed opening pair; these
       are the only games used for strength statistics. */
    int strength_games;
    int excluded_unpaired;
    int a_wins;
    int b_wins;
    int draws;
    int illegal;
    int timeouts;
    int node_limited;
    int aborted;
    int a_black_wins;
    int a_white_wins;
    int b_black_wins;
    int b_white_wins;
    double a_score;
    double score_low;
    double score_high;
    double elo;
    double elo_low;
    double elo_high;
    bool elo_defined;
    ExperimentEngineStats a_stats;
    ExperimentEngineStats b_stats;
    uint64_t peak_rss_kb;
} ExperimentSummary;

typedef struct ExperimentResult {
    ExperimentConfig config;
    ExperimentSummary summary;
    ExperimentGame *games;
    int game_count;
    ExperimentMoveSample *moves;
    int move_count;
    char commit[64];
    char os[128];
    char cpu[128];
    char compiler[128];
    char build_type[32];
    char data_version[48];
} ExperimentResult;

/* Parses a versioned key=value config; on failure writes a reason. */
bool experiment_config_parse(ExperimentConfig *config, const char *text,
                             char *error, size_t error_size);
bool experiment_config_load(ExperimentConfig *config, const char *path,
                            char *error, size_t error_size);

/*
 * Runs the paired match set. Results are allocated; call
 * experiment_result_free() when done. Engines are deterministic for a fixed
 * seed; time-budgeted searches still depend on the machine.
 */
bool experiment_run(const ExperimentConfig *config, ExperimentResult *result,
                    char *error, size_t error_size);
void experiment_result_free(ExperimentResult *result);

/*
 * Writes games.csv, moves.csv, summary.json, report.md and report.html into
 * dir (created when missing). The HTML chart reads the same summary object
 * that is written to JSON.
 */
bool experiment_write_all(const ExperimentResult *result, const char *dir,
                          char *error, size_t error_size);

/* Human-readable summary for CLI output. */
void experiment_print_summary(const ExperimentResult *result);

#endif
