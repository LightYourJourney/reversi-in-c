#include "ai/ai_worker.h"
#include "core/rules.h"
#include "test_harness.h"

#include <stdint.h>
#include <time.h>

static uint64_t now_ms(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool wait_finished(AiWorker *worker, int timeout_ms)
{
    const uint64_t start = now_ms();
    while (ai_worker_status(worker) == AI_WORKER_RUNNING) {
        if (now_ms() - start > (uint64_t)timeout_ms) {
            return false;
        }
    }
    return true;
}

static void test_start_and_take(TestStats *stats)
{
    AiWorker *worker = ai_worker_create();
    REVERSI_TEST_ASSERT(stats, worker != NULL);
    if (worker == NULL) {
        return;
    }

    GameState state;
    game_init(&state);
    const GameState snapshot = state;

    AiConfig config = ai_default_config(AI_DIFFICULTY_MEDIUM, 0);
    config.time_budget_ms = 40;
    config.max_depth = 3;
    REVERSI_TEST_ASSERT(stats, ai_worker_start(worker, &state, &config));
    REVERSI_TEST_ASSERT(stats, ai_worker_is_active(worker));
    REVERSI_TEST_ASSERT(stats,
        !ai_worker_start(worker, &state, &config));

    /* Mutating the caller's state must not affect the snapshot search. */
    state.current_player = STONE_WHITE;

    REVERSI_TEST_ASSERT(stats, wait_finished(worker, 3000));
    REVERSI_TEST_ASSERT(stats,
        ai_worker_status(worker) == AI_WORKER_READY);

    Move move = {0, 0};
    AiStats search;
    REVERSI_TEST_ASSERT(stats, ai_worker_take(worker, &move, &search));
    REVERSI_TEST_ASSERT(stats, rules_is_legal_move(
        &snapshot.board, STONE_BLACK, move));
    REVERSI_TEST_ASSERT(stats, search.nodes > 0);
    REVERSI_TEST_ASSERT(stats, !ai_worker_is_active(worker));
    REVERSI_TEST_ASSERT(stats,
        ai_worker_status(worker) == AI_WORKER_IDLE);
    REVERSI_TEST_ASSERT(stats,
        !ai_worker_take(worker, &move, NULL));

    ai_worker_destroy(worker);
}

static void test_cancel(TestStats *stats)
{
    AiWorker *worker = ai_worker_create();
    REVERSI_TEST_ASSERT(stats, worker != NULL);
    if (worker == NULL) {
        return;
    }

    GameState state;
    game_init(&state);
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0);
    config.time_budget_ms = 2000;
    config.max_depth = 12;
    REVERSI_TEST_ASSERT(stats, ai_worker_start(worker, &state, &config));
    ai_worker_cancel(worker);
    REVERSI_TEST_ASSERT(stats,
        ai_worker_status(worker) == AI_WORKER_FAILED);

    Move move = {0, 0};
    REVERSI_TEST_ASSERT(stats, !ai_worker_take(worker, &move, NULL));
    REVERSI_TEST_ASSERT(stats, !ai_worker_is_active(worker));

    ai_worker_join(worker);
    ai_worker_destroy(worker);
}

static void test_null_safety(TestStats *stats)
{
    GameState state;
    game_init(&state);
    AiConfig config = ai_default_config(AI_DIFFICULTY_EASY, 0);
    Move move = {0, 0};

    AiWorker *worker = ai_worker_create();
    REVERSI_TEST_ASSERT(stats, worker != NULL);
    ai_worker_destroy(worker);
    REVERSI_TEST_ASSERT(stats, !ai_worker_start(NULL, &state, &config));
    REVERSI_TEST_ASSERT(stats,
        ai_worker_status(NULL) == AI_WORKER_IDLE);
    REVERSI_TEST_ASSERT(stats, !ai_worker_take(NULL, &move, NULL));
    REVERSI_TEST_ASSERT(stats, !ai_worker_is_active(NULL));
    ai_worker_cancel(NULL);
    ai_worker_join(NULL);
    ai_worker_destroy(NULL);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_start_and_take);
    REVERSI_RUN_TEST(&stats, test_cancel);
    REVERSI_RUN_TEST(&stats, test_null_safety);

    return reversi_test_summary(&stats, "ai.worker");
}
