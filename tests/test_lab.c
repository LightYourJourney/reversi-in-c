#include "ai/ai_worker.h"
#include "ui/lab_view.h"
#include "test_harness.h"

#include <stdbool.h>
#include <string.h>

static void test_generation_guard(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats, lab_view_accepts_result(7, 7));
    REVERSI_TEST_ASSERT(stats, !lab_view_accepts_result(7, 8));
    REVERSI_TEST_ASSERT(stats, !lab_view_accepts_result(8, 7));
}

static void test_enter_leave(TestStats *stats)
{
    LabView view;
    lab_view_init(&view);
    lab_view_enter(&view, NULL, false);
    REVERSI_TEST_ASSERT(stats, analysis_main_length(&view.session) == 0);
    REVERSI_TEST_ASSERT(stats, analysis_current_kind(&view.session)
                        == ANALYSIS_POSITION_VALID);

    AiWorker *worker = ai_worker_create();
    REVERSI_TEST_ASSERT(stats, worker != NULL);
    if (worker == NULL) {
        return;
    }
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 3);
    config.max_depth = 4;
    config.time_budget_ms = 5000;
    const AnalysisPosition *pos = analysis_current(&view.session);
    REVERSI_TEST_ASSERT(stats, ai_worker_start_analysis(
        worker, &pos->state, &config, 3));
    REVERSI_TEST_ASSERT(stats, ai_worker_is_active(worker));
    lab_view_leave(&view, worker);
    REVERSI_TEST_ASSERT(stats, !ai_worker_is_active(worker));
    ai_worker_destroy(worker);
}

static void test_load_record(TestStats *stats)
{
    /* The app reads lab-record.rvr and hands the text to the view: a good
     * record replaces the session, a bad one leaves it and says why. */
    static LabView view;
    lab_view_init(&view);
    lab_view_enter(&view, NULL, false);
    AiWorker *worker = ai_worker_create();
    REVERSI_TEST_ASSERT(stats, worker != NULL);

    REVERSI_TEST_ASSERT(stats, lab_view_load_record(
        &view, worker, "reversi-record 1\nresult -\nmoves d3 c3\n"));
    REVERSI_TEST_ASSERT(stats, analysis_main_length(&view.session) == 2);
    REVERSI_TEST_ASSERT(stats, strcmp(view.notice, "Record loaded") == 0);

    REVERSI_TEST_ASSERT(stats, !lab_view_load_record(
        &view, worker, "reversi-record 1\nmoves d3 d3\n"));
    REVERSI_TEST_ASSERT(stats, analysis_main_length(&view.session) == 2);
    REVERSI_TEST_ASSERT(stats, strcmp(view.notice, "Record loaded") != 0);

    lab_view_leave(&view, worker);
    ai_worker_destroy(worker);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_generation_guard);
    REVERSI_RUN_TEST(&stats, test_enter_leave);
    REVERSI_RUN_TEST(&stats, test_load_record);
    return reversi_test_summary(&stats, "analysis.lab");
}
