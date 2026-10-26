#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "experiment/experiment.h"

#include "ai/ai_baselines.h"
#include "ai/ai_search.h"
#include "core/board.h"
#include "core/rules.h"
#include "reversi/version.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#define REVERSI_EXPERIMENT_CONFIG_MAX_BYTES 16384
#define REVERSI_EXPERIMENT_Z95 1.96

#ifndef REVERSI_EXPERIMENT_COMPILER
#define REVERSI_EXPERIMENT_COMPILER "unknown compiler"
#endif
#ifndef REVERSI_EXPERIMENT_BUILD_TYPE
#define REVERSI_EXPERIMENT_BUILD_TYPE "unknown"
#endif

static void set_error(char *error, size_t error_size, const char *text)
{
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", text);
    }
}

static void trim(char *text)
{
    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    size_t length = strlen(start);
    while (length > 0 && isspace((unsigned char)start[length - 1])) {
        --length;
    }
    memmove(text, start, length);
    text[length] = '\0';
}

static bool parse_int(const char *text, long min, long max, int *out)
{
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < min || value > max) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool parse_u64(const char *text, uint64_t max, uint64_t *out)
{
    char *end = NULL;
    const unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value > max) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool parse_bool(const char *text, bool *out)
{
    if (strcmp(text, "on") == 0 || strcmp(text, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(text, "off") == 0 || strcmp(text, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_engine(ExperimentEngine *engine, char *tokens,
                         char *error, size_t error_size)
{
    memset(engine, 0, sizeof(*engine));
    engine->kind = EXPERIMENT_ENGINE_SEARCH;
    engine->difficulty = AI_DIFFICULTY_HARD;
    engine->max_depth = 8;
    engine->time_budget_ms = 500;
    engine->max_nodes = 0;
    engine->use_tt = true;
    engine->use_endgame = true;
    engine->seed = 1;

    char *cursor = tokens;
    while (*cursor != '\0') {
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char *token = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
        char *equals = strchr(token, '=');
        if (equals == NULL) {
            set_error(error, error_size, "engine option must be key=value");
            return false;
        }
        *equals = '\0';
        const char *key = token;
        char *value = equals + 1;

        if (strcmp(key, "kind") == 0) {
            if (strcmp(value, "random") == 0) {
                engine->kind = EXPERIMENT_ENGINE_RANDOM;
            } else if (strcmp(value, "greedy") == 0) {
                engine->kind = EXPERIMENT_ENGINE_GREEDY;
            } else if (strcmp(value, "ai") == 0) {
                engine->kind = EXPERIMENT_ENGINE_AI;
            } else if (strcmp(value, "search") == 0) {
                engine->kind = EXPERIMENT_ENGINE_SEARCH;
            } else {
                set_error(error, error_size, "unknown engine kind");
                return false;
            }
        } else if (strcmp(key, "difficulty") == 0) {
            if (strcmp(value, "easy") == 0) {
                engine->difficulty = AI_DIFFICULTY_EASY;
            } else if (strcmp(value, "medium") == 0) {
                engine->difficulty = AI_DIFFICULTY_MEDIUM;
            } else if (strcmp(value, "hard") == 0) {
                engine->difficulty = AI_DIFFICULTY_HARD;
            } else {
                set_error(error, error_size, "unknown difficulty");
                return false;
            }
        } else if (strcmp(key, "depth") == 0) {
            if (!parse_int(value, 1, 32, &engine->max_depth)) {
                set_error(error, error_size, "invalid depth");
                return false;
            }
        } else if (strcmp(key, "time") == 0) {
            if (!parse_int(value, 1, 10000, &engine->time_budget_ms)) {
                set_error(error, error_size, "invalid time budget");
                return false;
            }
        } else if (strcmp(key, "nodes") == 0) {
            if (!parse_u64(value, REVERSI_AI_MAX_NODES, &engine->max_nodes)) {
                set_error(error, error_size, "invalid node budget");
                return false;
            }
        } else if (strcmp(key, "tt") == 0) {
            if (!parse_bool(value, &engine->use_tt)) {
                set_error(error, error_size, "invalid tt flag");
                return false;
            }
        } else if (strcmp(key, "endgame") == 0) {
            if (!parse_bool(value, &engine->use_endgame)) {
                set_error(error, error_size, "invalid endgame flag");
                return false;
            }
        } else if (strcmp(key, "label") == 0) {
            snprintf(engine->name, sizeof(engine->name), "%s", value);
        } else if (strcmp(key, "seed") == 0) {
            unsigned seed = 0;
            if (sscanf(value, "%u", &seed) != 1) {
                set_error(error, error_size, "invalid engine seed");
                return false;
            }
            engine->seed = seed;
        } else {
            set_error(error, error_size, "unknown engine option");
            return false;
        }
    }
    if (engine->name[0] == '\0') {
        snprintf(engine->name, sizeof(engine->name), "engine");
    }
    return true;
}

bool experiment_config_parse(ExperimentConfig *config, const char *text,
                             char *error, size_t error_size)
{
    if (config == NULL || text == NULL) {
        set_error(error, error_size, "invalid arguments");
        return false;
    }
    memset(config, 0, sizeof(*config));
    snprintf(config->name, sizeof(config->name), "experiment");
    config->seed = 1;
    config->games = 8;
    config->openings = 2;
    config->opening_plies = 4;
    config->max_plies = 100;

    char work[REVERSI_EXPERIMENT_CONFIG_MAX_BYTES];
    const size_t length = strlen(text);
    if (length >= sizeof(work)) {
        set_error(error, error_size, "config is too large");
        return false;
    }
    memcpy(work, text, length + 1);

    bool have_version = false;
    bool have_a = false;
    bool have_b = false;
    char *line = work;
    while (*line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            ++next;
        }
        char *after = next != NULL ? next : line + strlen(line);
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            line = after;
            continue;
        }

        char *space = line;
        while (*space != '\0' && !isspace((unsigned char)*space)) {
            ++space;
        }
        if (*space != '\0') {
            *space++ = '\0';
        }
        trim(space);
        const char *key = line;
        char *value = space;

        if (strcmp(key, "version") == 0) {
            int version = 0;
            if (!parse_int(value, 1, 1, &version)) {
                set_error(error, error_size, "unsupported config version");
                return false;
            }
            have_version = true;
        } else if (strcmp(key, "name") == 0) {
            if (value[0] == '\0' || strlen(value) >= sizeof(config->name)) {
                set_error(error, error_size, "invalid experiment name");
                return false;
            }
            snprintf(config->name, sizeof(config->name), "%s", value);
        } else if (strcmp(key, "seed") == 0) {
            unsigned seed = 0;
            if (sscanf(value, "%u", &seed) != 1) {
                set_error(error, error_size, "invalid seed");
                return false;
            }
            config->seed = seed;
        } else if (strcmp(key, "games") == 0) {
            if (!parse_int(value, 2, REVERSI_EXPERIMENT_MAX_GAMES,
                           &config->games)
                || (config->games % 2) != 0) {
                set_error(error, error_size,
                          "games must be even and within range");
                return false;
            }
        } else if (strcmp(key, "openings") == 0) {
            if (!parse_int(value, 1, REVERSI_EXPERIMENT_MAX_OPENINGS,
                           &config->openings)) {
                set_error(error, error_size, "invalid opening count");
                return false;
            }
        } else if (strcmp(key, "opening-ply") == 0) {
            if (!parse_int(value, 0, 32, &config->opening_plies)) {
                set_error(error, error_size, "invalid opening-ply");
                return false;
            }
        } else if (strcmp(key, "max-plies") == 0) {
            if (!parse_int(value, 1, REVERSI_EXPERIMENT_MAX_PLIES,
                           &config->max_plies)) {
                set_error(error, error_size, "invalid max-plies");
                return false;
            }
        } else if (strcmp(key, "engine-a") == 0) {
            if (!parse_engine(&config->engines[0], value, error,
                              error_size)) {
                return false;
            }
            have_a = true;
        } else if (strcmp(key, "engine-b") == 0) {
            if (!parse_engine(&config->engines[1], value, error,
                              error_size)) {
                return false;
            }
            have_b = true;
        } else {
            set_error(error, error_size, "unknown config key");
            return false;
        }
        line = after;
    }

    if (!have_version || !have_a || !have_b) {
        set_error(error, error_size,
                  "config needs version, engine-a and engine-b");
        return false;
    }
    return true;
}

bool experiment_config_load(ExperimentConfig *config, const char *path,
                            char *error, size_t error_size)
{
    if (config == NULL || path == NULL) {
        set_error(error, error_size, "invalid arguments");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_size, "cannot open config");
        return false;
    }
    char text[REVERSI_EXPERIMENT_CONFIG_MAX_BYTES];
    const size_t read = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[read] = '\0';
    return experiment_config_parse(config, text, error, error_size);
}

static void capture_git(ExperimentResult *result)
{
    snprintf(result->commit, sizeof(result->commit), "%s",
             REVERSI_VERSION_COMMIT);
#if defined(_WIN32)
    FILE *sha = _popen("git rev-parse --short HEAD", "r");
#else
    FILE *sha = popen("git rev-parse --short HEAD", "r");
#endif
    char line[128];
    if (sha != NULL) {
        if (fgets(line, sizeof(line), sha) != NULL) {
            trim(line);
            if (line[0] != '\0' && strlen(line) < sizeof(result->commit)) {
                snprintf(result->commit, sizeof(result->commit), "%s", line);
            }
        }
#if defined(_WIN32)
        _pclose(sha);
#else
        pclose(sha);
#endif
    }
#if defined(_WIN32)
    FILE *dirty = _popen("git status --porcelain -- . \":(exclude)docs/experiments\"", "r");
#else
    FILE *dirty = popen("git status --porcelain -- . \":(exclude)docs/experiments\"", "r");
#endif
    if (dirty != NULL) {
        if (fgets(line, sizeof(line), dirty) != NULL) {
            const size_t used = strlen(result->commit);
            if (used + 7 < sizeof(result->commit)) {
                snprintf(result->commit + used,
                         sizeof(result->commit) - used, "-dirty");
            }
        }
#if defined(_WIN32)
        _pclose(dirty);
#else
        pclose(dirty);
#endif
    }
}

static void capture_manifest(ExperimentResult *result)
{
    capture_git(result);
    snprintf(result->compiler, sizeof(result->compiler), "%s",
             REVERSI_EXPERIMENT_COMPILER);
    snprintf(result->build_type, sizeof(result->build_type), "%s",
             REVERSI_EXPERIMENT_BUILD_TYPE);
    snprintf(result->data_version, sizeof(result->data_version),
             "config-1/raw-1/openings-1");

#if defined(_WIN32)
    snprintf(result->os, sizeof(result->os), "Windows");
    const char *cpu = getenv("PROCESSOR_IDENTIFIER");
    snprintf(result->cpu, sizeof(result->cpu), "%s",
             cpu != NULL ? cpu : "unknown x86");
#else
    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(result->os, sizeof(result->os), "%s %s %s", uts.sysname,
                 uts.release, uts.machine);
    } else {
        snprintf(result->os, sizeof(result->os), "unknown");
    }
    result->cpu[0] = '\0';
#if defined(__APPLE__)
    size_t size = sizeof(result->cpu);
    if (sysctlbyname("machdep.cpu.brand_string", result->cpu, &size, NULL,
                     0) != 0) {
        result->cpu[0] = '\0';
    }
#elif defined(__linux__)
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo) != NULL) {
            if (strncmp(line, "model name", 10) == 0) {
                const char *colon = strchr(line, ':');
                if (colon != NULL) {
                    snprintf(result->cpu, sizeof(result->cpu), "%s",
                             colon + 1);
                }
                break;
            }
        }
        fclose(cpuinfo);
    }
#endif
    trim(result->cpu);
    if (result->cpu[0] == '\0') {
        snprintf(result->cpu, sizeof(result->cpu), "unknown CPU");
    }
#endif
}

static uint64_t peak_rss_kb(void)
{
#if defined(_WIN32)
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return (uint64_t)usage.ru_maxrss / 1024;
#else
    return (uint64_t)usage.ru_maxrss;
#endif
#endif
}

static uint64_t mix_seed(uint64_t seed, int game, int engine)
{
    uint64_t value = seed ^ ((uint64_t)game * 0x9E3779B97F4A7C15ULL)
        ^ ((uint64_t)(engine + 1) * 0xBF58476D1CE4E5B9ULL);
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    return value;
}

static GameState generate_opening(uint64_t seed, int ply)
{
    GameState state;
    game_init(&state);
    uint64_t rng = seed != 0 ? seed : 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < ply && !game_is_over(&state); ++i) {
        if (game_must_pass(&state)) {
            if (!game_pass(&state)) {
                break;
            }
            continue;
        }
        const Move move = ai_baseline_random_move(&state, &rng);
        if (move.row < 0 || !game_apply_move(&state, move)) {
            break;
        }
    }
    return state;
}

static int compare_int(const void *left, const void *right)
{
    const int a = *(const int *)left;
    const int b = *(const int *)right;
    return (a > b) - (a < b);
}

static void finalize_engine_stats(ExperimentEngineStats *stats,
                                  const ExperimentMoveSample *moves,
                                  int count, int engine)
{
    int *samples = malloc((size_t)(count > 0 ? count : 1) * sizeof(int));
    int used = 0;
    uint64_t depth_sum = 0;
    for (int i = 0; i < count; ++i) {
        if (moves[i].engine != engine) {
            continue;
        }
        ++stats->moves;
        stats->nodes += moves[i].nodes;
        stats->cutoffs += moves[i].cutoffs;
        stats->tt_hits += moves[i].tt_hits;
        stats->elapsed_ms += (uint64_t)moves[i].elapsed_ms;
        depth_sum += (uint64_t)moves[i].depth;
        if (moves[i].depth > stats->max_depth) {
            stats->max_depth = moves[i].depth;
        }
        if (moves[i].timed_out) {
            ++stats->timeouts;
        }
        if (moves[i].node_limit) {
            ++stats->node_limits;
        }
        if (moves[i].cancelled) {
            ++stats->cancellations;
        }
        if (samples != NULL) {
            samples[used++] = moves[i].elapsed_ms;
        }
    }
    if (samples != NULL && used > 0) {
        qsort(samples, (size_t)used, sizeof(int), compare_int);
        stats->p50_ms = samples[(used - 1) * 50 / 100];
        stats->p95_ms = samples[(used - 1) * 95 / 100];
        stats->max_ms = samples[used - 1];
    }
    if (stats->moves > 0) {
        stats->avg_ms = (int)(stats->elapsed_ms
                              / (uint64_t)stats->moves);
        stats->avg_depth = (int)(depth_sum / (uint64_t)stats->moves);
    }
    if (stats->elapsed_ms > 0) {
        stats->nps = (double)stats->nodes * 1000.0
            / (double)stats->elapsed_ms;
    }
    free(samples);
}

static void wilson_mean(double p, int units, double *low, double *high)
{
    if (units <= 0) {
        *low = 0.0;
        *high = 1.0;
        return;
    }
    const double n = (double)units;
    const double z2 = REVERSI_EXPERIMENT_Z95 * REVERSI_EXPERIMENT_Z95;
    const double denom = 1.0 + z2 / n;
    const double center = (p + z2 / (2.0 * n)) / denom;
    const double half = REVERSI_EXPERIMENT_Z95
        * sqrt((p * (1.0 - p) + z2 / (4.0 * n)) / n) / denom;
    *low = center - half;
    *high = center + half;
    if (*low < 0.0) {
        *low = 0.0;
    }
    if (*high > 1.0) {
        *high = 1.0;
    }
}

static double elo_from_score(double score)
{
    const double clamped = score < 0.001 ? 0.001
        : (score > 0.999 ? 0.999 : score);
    return -400.0 * log10(1.0 / clamped - 1.0);
}

void experiment_result_free(ExperimentResult *result)
{
    if (result == NULL) {
        return;
    }
    free(result->games);
    free(result->moves);
    result->games = NULL;
    result->moves = NULL;
    result->game_count = 0;
    result->move_count = 0;
}

static bool experiment_config_valid(const ExperimentConfig *config)
{
    if (config == NULL || config->name[0] == '\0'
        || config->games < 2
        || config->games > REVERSI_EXPERIMENT_MAX_GAMES
        || (config->games % 2) != 0
        || config->openings < 1
        || config->openings > REVERSI_EXPERIMENT_MAX_OPENINGS
        || config->opening_plies < 0 || config->opening_plies > 32
        || config->max_plies < 1
        || config->max_plies > REVERSI_EXPERIMENT_MAX_PLIES) {
        return false;
    }
    for (int i = 0; i < REVERSI_EXPERIMENT_MAX_ENGINES; ++i) {
        const ExperimentEngine *engine = &config->engines[i];
        if (engine->name[0] == '\0'
            || engine->kind > EXPERIMENT_ENGINE_SEARCH
            || engine->difficulty > AI_DIFFICULTY_HARD
            || engine->max_depth < 1 || engine->max_depth > 32
            || engine->time_budget_ms < 1
            || engine->time_budget_ms > 10000
            || engine->max_nodes > REVERSI_AI_MAX_NODES) {
            return false;
        }
    }
    return true;
}

bool experiment_run(const ExperimentConfig *config, ExperimentResult *result,
                    char *error, size_t error_size)
{
    if (!experiment_config_valid(config) || result == NULL) {
        set_error(error, error_size, "invalid experiment config");
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->config = *config;
    capture_manifest(result);

    GameState openings[REVERSI_EXPERIMENT_MAX_OPENINGS];
    for (int i = 0; i < config->openings; ++i) {
        openings[i] = generate_opening(
            (uint64_t)config->seed + (uint64_t)i * 0x9E3779B97F4A7C15ULL,
            config->opening_plies);
    }

    result->games = calloc((size_t)config->games, sizeof(ExperimentGame));
    result->moves = calloc((size_t)config->games
                           * (size_t)config->max_plies,
                           sizeof(ExperimentMoveSample));
    if (result->games == NULL || result->moves == NULL) {
        experiment_result_free(result);
        set_error(error, error_size, "out of memory");
        return false;
    }
    result->game_count = config->games;
    result->move_count = 0;

    for (int game = 0; game < config->games; ++game) {
        ExperimentGame *record = &result->games[game];
        record->index = game;
        record->opening = (game / 2) % config->openings;
        const bool a_black = (game % 2) == 0;
        record->a_color = a_black ? STONE_BLACK : STONE_WHITE;

        GameState state = openings[record->opening];
        uint64_t rng[2] = {
            mix_seed((uint64_t)config->seed
                     ^ config->engines[0].seed, game, 0),
            mix_seed((uint64_t)config->seed
                     ^ config->engines[1].seed, game, 1)
        };
        int ply = 0;
        for (; ply < config->max_plies
             && state.result == GAME_RESULT_ONGOING; ++ply) {
            if (game_must_pass(&state)) {
                if (!game_pass(&state)) {
                    record->illegal = true;
                    break;
                }
                ++record->passes;
                continue;
            }

            const int engine = (state.current_player == STONE_BLACK)
                    == a_black ? 0 : 1;
            const ExperimentEngine *spec = &config->engines[engine];
            AiConfig ai = ai_default_config(spec->difficulty,
                                            (unsigned)mix_seed(
                                                (uint64_t)config->seed
                                                ^ spec->seed, game,
                                                engine));
            ai.max_depth = spec->max_depth;
            ai.time_budget_ms = spec->time_budget_ms;
            ai.max_nodes = spec->max_nodes;

            Move move = {-1, -1};
            AiStats stats;
            memset(&stats, 0, sizeof(stats));
            bool ok = false;
            switch (spec->kind) {
            case EXPERIMENT_ENGINE_RANDOM:
                move = ai_baseline_random_move(&state, &rng[engine]);
                ok = move.row >= 0;
                break;
            case EXPERIMENT_ENGINE_GREEDY:
                move = ai_baseline_greedy_move(&state);
                ok = move.row >= 0;
                break;
            case EXPERIMENT_ENGINE_AI:
                ok = ai_choose_move(&state, &ai, &move, &stats);
                break;
            case EXPERIMENT_ENGINE_SEARCH:
            default:
                ok = ai_search_choose_flags(&state, &ai, spec->use_tt,
                                            spec->use_endgame, &move,
                                            &stats);
                break;
            }

            if (!ok) {
                if (stats.node_limit) {
                    record->node_limit = true;
                } else if (stats.timed_out || stats.cancelled) {
                    record->timed_out = true;
                } else {
                    record->illegal = true;
                }
                break;
            }
            if (move.row < 0
                || !rules_is_legal_move(&state.board, state.current_player,
                                        move)) {
                record->illegal = true;
                break;
            }

            if (result->move_count
                < config->games * config->max_plies) {
                ExperimentMoveSample *sample =
                    &result->moves[result->move_count++];
                sample->game = game;
                sample->ply = ply;
                sample->engine = engine;
                sample->color = state.current_player;
                sample->move = move;
                sample->depth = stats.depth;
                sample->score = stats.score;
                sample->nodes = stats.nodes;
                sample->cutoffs = stats.cutoffs;
                sample->tt_hits = stats.tt_hits;
                sample->elapsed_ms = stats.elapsed_ms;
                sample->cancelled = stats.cancelled;
                sample->timed_out = stats.timed_out;
                sample->node_limit = stats.node_limit;
            }

            if (!game_apply_move(&state, move)) {
                record->illegal = true;
                break;
            }
        }

        record->plies = ply;
        record->result = state.result;
        record->black_discs = board_count(&state.board, STONE_BLACK);
        record->white_discs = board_count(&state.board, STONE_WHITE);
        if (!record->illegal && !record->timed_out
            && !record->node_limit
            && state.result == GAME_RESULT_ONGOING) {
            record->aborted = true;
        }
    }

    ExperimentSummary *summary = &result->summary;
    summary->games = config->games;
    for (int i = 0; i < config->games; ++i) {
        const ExperimentGame *game = &result->games[i];
        if (game->illegal) {
            ++summary->illegal;
            continue;
        }
        if (game->timed_out) {
            ++summary->timeouts;
            continue;
        }
        if (game->node_limit) {
            ++summary->node_limited;
            continue;
        }
        if (game->aborted) {
            ++summary->aborted;
            continue;
        }
        ++summary->completed;
        if (game->result == GAME_RESULT_DRAW) {
            ++summary->draws;
        } else {
            const bool a_won =
                (game->result == GAME_RESULT_BLACK_WIN) == (game->a_color
                                                            == STONE_BLACK);
            if (a_won) {
                ++summary->a_wins;
                if (game->a_color == STONE_BLACK) {
                    ++summary->a_black_wins;
                } else {
                    ++summary->a_white_wins;
                }
            } else {
                ++summary->b_wins;
                if (game->a_color == STONE_BLACK) {
                    ++summary->b_white_wins;
                } else {
                    ++summary->b_black_wins;
                }
            }
        }
    }

    if (summary->completed > 0) {
        summary->a_score = ((double)summary->a_wins
                            + 0.5 * (double)summary->draws)
            / (double)summary->completed;
        /* Games are paired by opening with colors swapped, so the
           independent unit is a pair, not a single game. */
        const int pairs = (summary->completed + 1) / 2;
        wilson_mean(summary->a_score, pairs, &summary->score_low,
                    &summary->score_high);
        summary->elo = elo_from_score(summary->a_score);
        summary->elo_low = elo_from_score(summary->score_low);
        summary->elo_high = elo_from_score(summary->score_high);
        if (fabs(summary->elo) < 0.5) {
            summary->elo = 0.0;
        }
        if (fabs(summary->elo_low) < 0.5) {
            summary->elo_low = 0.0;
        }
        if (fabs(summary->elo_high) < 0.5) {
            summary->elo_high = 0.0;
        }
        summary->elo_defined = true;
    }
    summary->peak_rss_kb = peak_rss_kb();

    finalize_engine_stats(&summary->a_stats, result->moves,
                          result->move_count, 0);
    finalize_engine_stats(&summary->b_stats, result->moves,
                          result->move_count, 1);
    return true;
}

void experiment_print_summary(const ExperimentResult *result)
{
    if (result == NULL) {
        return;
    }
    const ExperimentSummary *s = &result->summary;
    printf("Experiment %s: %d games (%d completed)\n",
           result->config.name, s->games, s->completed);
    if (s->elo_defined) {
        printf("  %s: W=%d D=%d L=%d  score=%.3f  Elo=%+.0f [%+.0f, %+.0f]\n",
               result->config.engines[0].name, s->a_wins, s->draws,
               s->b_wins, s->a_score, s->elo, s->elo_low, s->elo_high);
    } else {
        printf("  %s: W=%d D=%d L=%d  score=n/a (no completed games)\n",
               result->config.engines[0].name, s->a_wins, s->draws,
               s->b_wins);
    }
    printf("  illegal=%d timeouts=%d aborted=%d\n", s->illegal,
           s->timeouts, s->aborted);
    printf("  A moves=%d nodes=%" PRIu64 " p50=%dms p95=%dms depth=%d\n",
           s->a_stats.moves, s->a_stats.nodes, s->a_stats.p50_ms,
           s->a_stats.p95_ms, s->a_stats.max_depth);
    printf("  B moves=%d nodes=%" PRIu64 " p50=%dms p95=%dms depth=%d\n",
           s->b_stats.moves, s->b_stats.nodes, s->b_stats.p50_ms,
           s->b_stats.p95_ms, s->b_stats.max_depth);
}
