#include "experiment/experiment.h"
#include "test_harness.h"

#include <stdio.h>
#include <string.h>

static const char *REGRESSION_CONFIG =
    "version 1\n"
    "name regression\n"
    "seed 7\n"
    "games 4\n"
    "openings 2\n"
    "opening-ply 2\n"
    "max-plies 100\n"
    "engine-a kind=random label=Random\n"
    "engine-b kind=greedy label=Greedy\n";

static void test_config_parse(TestStats *stats)
{
    ExperimentConfig config;
    char error[128] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, REGRESSION_CONFIG, error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, config.games == 4);
    REVERSI_TEST_ASSERT(stats, config.engines[0].kind
                        == EXPERIMENT_ENGINE_RANDOM);
    REVERSI_TEST_ASSERT(stats, config.engines[1].kind
                        == EXPERIMENT_ENGINE_GREEDY);

    REVERSI_TEST_ASSERT(stats, !experiment_config_parse(
        &config, "version 2\ngames 2\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !experiment_config_parse(
        &config, "version 1\ngames 3\nengine-a kind=random\n"
                 "engine-b kind=greedy\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !experiment_config_parse(
        &config, "version 1\ngames 2\nengine-a kind=random\n",
        error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !experiment_config_parse(
        &config, "version 1\nbogus 1\nengine-a kind=random\n"
                 "engine-b kind=greedy\n", error, sizeof(error)));
}

static void test_run_and_reports(TestStats *stats)
{
    ExperimentConfig config;
    char error[256] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, REGRESSION_CONFIG, error, sizeof(error)));

    ExperimentResult first;
    REVERSI_TEST_ASSERT(stats, experiment_run(&config, &first, error,
                                              sizeof(error)));
    REVERSI_TEST_ASSERT(stats, first.game_count == 4);
    REVERSI_TEST_ASSERT(stats, first.summary.illegal == 0);
    REVERSI_TEST_ASSERT(stats, first.summary.timeouts == 0);
    REVERSI_TEST_ASSERT(stats, first.summary.aborted == 0);
    REVERSI_TEST_ASSERT(stats, first.summary.completed == 4);
    REVERSI_TEST_ASSERT(stats, first.summary.a_wins
                        + first.summary.b_wins + first.summary.draws == 4);
    /* Paired colors: games 0 and 1 share an opening and swap colors. */
    REVERSI_TEST_ASSERT(stats, first.games[0].opening
                        == first.games[1].opening);
    REVERSI_TEST_ASSERT(stats, first.games[0].a_color
                        != first.games[1].a_color);

    /* Deterministic regression oracle: a change to rules, greedy evaluation
       or move selection that alters these outcomes must be deliberate. */
    static const GameResult expected[4] = {
        GAME_RESULT_BLACK_WIN, GAME_RESULT_WHITE_WIN,
        GAME_RESULT_WHITE_WIN, GAME_RESULT_BLACK_WIN
    };
    static const int expected_plies[4] = {58, 58, 58, 58};
    for (int i = 0; i < 4; ++i) {
        REVERSI_TEST_ASSERT(stats, first.games[i].result == expected[i]);
        REVERSI_TEST_ASSERT(stats, first.games[i].plies
                            == expected_plies[i]);
    }

    ExperimentResult second;
    REVERSI_TEST_ASSERT(stats, experiment_run(&config, &second, error,
                                              sizeof(error)));
    bool identical = true;
    for (int i = 0; i < first.game_count; ++i) {
        if (first.games[i].result != second.games[i].result
            || first.games[i].plies != second.games[i].plies
            || first.games[i].a_color != second.games[i].a_color) {
            identical = false;
        }
    }
    REVERSI_TEST_ASSERT(stats, identical);

    REVERSI_TEST_ASSERT(stats, experiment_write_all(
        &first, "experiment-test", error, sizeof(error)));
    const char *files[] = {"games.csv", "moves.csv", "summary.json",
                           "report.md", "report.html"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        char path[128];
        snprintf(path, sizeof(path), "experiment-test/%s", files[i]);
        FILE *file = fopen(path, "rb");
        REVERSI_TEST_ASSERT(stats, file != NULL);
        if (file != NULL) {
            fseek(file, 0, SEEK_END);
            REVERSI_TEST_ASSERT(stats, ftell(file) > 0);
            fclose(file);
        }
    }
    FILE *json = fopen("experiment-test/summary.json", "rb");
    if (json != NULL) {
        char text[4096];
        const size_t read = fread(text, 1, sizeof(text) - 1, json);
        text[read] = '\0';
        fclose(json);
        REVERSI_TEST_ASSERT(stats, strstr(text, "\"seed\":7") != NULL);
        REVERSI_TEST_ASSERT(stats, strstr(text,
            "\"format\":\"reversi-experiment-1\"") != NULL);
    }

    experiment_result_free(&first);
    experiment_result_free(&second);
}

static void test_search_regression(TestStats *stats)
{
    const char *config_text =
        "version 1\n"
        "name regression-search\n"
        "seed 11\n"
        "games 4\n"
        "openings 2\n"
        "opening-ply 2\n"
        "max-plies 100\n"
        "engine-a kind=search difficulty=hard depth=3 time=5000 "
        "tt=on endgame=off label=TT\n"
        "engine-b kind=search difficulty=hard depth=3 time=5000 "
        "tt=off endgame=off label=NoTT\n";
    ExperimentConfig config;
    char error[256] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, config_text, error, sizeof(error)));
    ExperimentResult result;
    REVERSI_TEST_ASSERT(stats, experiment_run(&config, &result, error,
                                              sizeof(error)));
    REVERSI_TEST_ASSERT(stats, result.summary.illegal == 0);
    REVERSI_TEST_ASSERT(stats, result.summary.timeouts == 0);
    REVERSI_TEST_ASSERT(stats, result.summary.aborted == 0);
    /* Node counts are a deterministic work metric across machines, unlike
       wall-clock time. A 10%+ change means the search path changed. */
    REVERSI_TEST_ASSERT(stats, result.summary.a_stats.nodes == 21447);
    REVERSI_TEST_ASSERT(stats, result.summary.b_stats.nodes == 25601);
    REVERSI_TEST_ASSERT(stats, result.summary.a_stats.max_depth == 3);
    REVERSI_TEST_ASSERT(stats, result.summary.b_stats.max_depth == 3);

    static const int expected_black[4] = {42, 42, 52, 52};
    for (int i = 0; i < 4; ++i) {
        REVERSI_TEST_ASSERT(stats, result.games[i].result
                            == GAME_RESULT_BLACK_WIN);
        REVERSI_TEST_ASSERT(stats, result.games[i].plies == 58);
        REVERSI_TEST_ASSERT(stats, result.games[i].black_discs
                            == expected_black[i]);
    }
    experiment_result_free(&result);
}

static void test_invalid_run_config(TestStats *stats)
{
    ExperimentConfig config;
    char error[128] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, REGRESSION_CONFIG, error, sizeof(error)));

    ExperimentResult result;
    config.openings = 0;
    REVERSI_TEST_ASSERT(stats, !experiment_run(&config, &result, error,
                                              sizeof(error)));
    config.openings = REVERSI_EXPERIMENT_MAX_OPENINGS + 1;
    REVERSI_TEST_ASSERT(stats, !experiment_run(&config, &result, error,
                                              sizeof(error)));
    config.openings = 2;
    config.max_plies = 0;
    REVERSI_TEST_ASSERT(stats, !experiment_run(&config, &result, error,
                                              sizeof(error)));
    config.max_plies = 100;
    config.engines[0].max_depth = 0;
    REVERSI_TEST_ASSERT(stats, !experiment_run(&config, &result, error,
                                              sizeof(error)));
}

static void test_node_limit_classification(TestStats *stats)
{
    const char *config_text =
        "version 1\n"
        "name node-cap-smoke\n"
        "seed 5\n"
        "games 2\n"
        "openings 1\n"
        "opening-ply 0\n"
        "max-plies 10\n"
        "engine-a kind=search difficulty=hard depth=5 time=1000 "
        "nodes=1 tt=on endgame=off\n"
        "engine-b kind=search difficulty=hard depth=5 time=1000 "
        "nodes=1 tt=on endgame=off\n";
    ExperimentConfig config;
    char error[128] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, config_text, error, sizeof(error)));
    ExperimentResult result;
    REVERSI_TEST_ASSERT(stats, experiment_run(&config, &result, error,
                                              sizeof(error)));
    REVERSI_TEST_ASSERT(stats, result.summary.node_limited == 2);
    REVERSI_TEST_ASSERT(stats, result.summary.timeouts == 0);
    REVERSI_TEST_ASSERT(stats, result.games[0].node_limit);
    REVERSI_TEST_ASSERT(stats, !result.games[0].timed_out);
    experiment_result_free(&result);
}

static void test_search_engine(TestStats *stats)
{
    const char *config_text =
        "version 1\n"
        "name search-smoke\n"
        "seed 3\n"
        "games 2\n"
        "openings 1\n"
        "opening-ply 2\n"
        "max-plies 60\n"
        "engine-a kind=search difficulty=hard depth=2 time=1000 "
        "tt=on endgame=off label=TT\n"
        "engine-b kind=search difficulty=hard depth=2 time=1000 "
        "tt=off endgame=off label=NoTT\n";
    ExperimentConfig config;
    char error[256] = {0};
    REVERSI_TEST_ASSERT(stats, experiment_config_parse(
        &config, config_text, error, sizeof(error)));
    ExperimentResult result;
    REVERSI_TEST_ASSERT(stats, experiment_run(&config, &result, error,
                                              sizeof(error)));
    REVERSI_TEST_ASSERT(stats, result.summary.illegal == 0);
    REVERSI_TEST_ASSERT(stats, result.summary.timeouts == 0);
    REVERSI_TEST_ASSERT(stats, result.move_count > 0);
    experiment_result_free(&result);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_config_parse);
    REVERSI_RUN_TEST(&stats, test_run_and_reports);
    REVERSI_RUN_TEST(&stats, test_search_regression);
    REVERSI_RUN_TEST(&stats, test_invalid_run_config);
    REVERSI_RUN_TEST(&stats, test_node_limit_classification);
    REVERSI_RUN_TEST(&stats, test_search_engine);
    return reversi_test_summary(&stats, "experiment.regression");
}
