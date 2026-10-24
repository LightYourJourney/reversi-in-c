#include "experiment/experiment.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
                "usage: %s <config.exp> <output-dir>\n", argv[0]);
        return 2;
    }
    ExperimentConfig config;
    char error[256] = {0};
    if (!experiment_config_load(&config, argv[1], error, sizeof(error))) {
        fprintf(stderr, "config: %s\n", error);
        return 1;
    }
    ExperimentResult result;
    if (!experiment_run(&config, &result, error, sizeof(error))) {
        fprintf(stderr, "run: %s\n", error);
        return 1;
    }
    experiment_print_summary(&result);
    if (!experiment_write_all(&result, argv[2], error, sizeof(error))) {
        fprintf(stderr, "report: %s\n", error);
        experiment_result_free(&result);
        return 1;
    }
    experiment_result_free(&result);
    return 0;
}
