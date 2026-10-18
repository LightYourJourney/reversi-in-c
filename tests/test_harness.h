#ifndef REVERSI_TESTS_TEST_HARNESS_H
#define REVERSI_TESTS_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>

typedef struct TestStats {
    int checks;
    int failures;
} TestStats;

#define REVERSI_TEST_ASSERT(stats, condition)                                 \
    do {                                                                      \
        ++(stats)->checks;                                                    \
        if (!(condition)) {                                                   \
            ++(stats)->failures;                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n",                  \
                    __FILE__, __LINE__, #condition);                          \
        }                                                                     \
    } while (0)

#define REVERSI_RUN_TEST(stats, test_function)                                \
    do {                                                                      \
        printf("RUN  %s\n", #test_function);                                  \
        test_function(stats);                                                 \
    } while (0)

static inline int reversi_test_summary(const TestStats *stats,
                                       const char *suite)
{
    if (stats->failures == 0) {
        printf("PASS %s (%d checks)\n", suite, stats->checks);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "FAIL %s (%d/%d checks failed)\n",
            suite, stats->failures, stats->checks);
    return EXIT_FAILURE;
}

#endif
