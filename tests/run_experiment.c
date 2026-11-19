#include "experiment/experiment.h"

#include <inttypes.h>
#include <stdio.h>

/* Human-readable summary; printing belongs to the tool, not the library. */
static void print_summary(const ExperimentResult *result)
{
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
    print_summary(&result);
    if (!experiment_write_all(&result, argv[2], error, sizeof(error))) {
        fprintf(stderr, "report: %s\n", error);
        experiment_result_free(&result);
        return 1;
    }
    experiment_result_free(&result);
    return 0;
}
