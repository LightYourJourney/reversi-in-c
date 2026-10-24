#include "ai/ai_worker.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef enum AiWorkerKind {
    AI_WORKER_JOB_MOVE = 0,
    AI_WORKER_JOB_ANALYSIS
} AiWorkerKind;

typedef struct AiWorkerJob {
    AiWorkerKind kind;
    GameState state;
    AiConfig config;
    int max_lines;
    Move move;
    AiStats stats;
    AiAnalysis analysis;
    bool found;
    atomic_bool cancel;
    atomic_bool done;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} AiWorkerJob;

struct AiWorker {
    AiWorkerJob *job;
};

static bool job_should_cancel(void *context)
{
    const AiWorkerJob *job = context;
    return atomic_load_explicit(&job->cancel, memory_order_acquire);
}

#if defined(_WIN32)
static DWORD WINAPI worker_main(LPVOID argument)
#else
static void *worker_main(void *argument)
#endif
{
    AiWorkerJob *job = argument;
    if (job->kind == AI_WORKER_JOB_ANALYSIS) {
        job->found = ai_analyze_position(&job->state, &job->config,
                                         job->max_lines, &job->analysis);
    } else {
        job->found = ai_choose_move(&job->state, &job->config,
                                    &job->move, &job->stats);
    }
    atomic_store_explicit(&job->done, true, memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static bool start_thread(AiWorkerJob *job)
{
#if defined(_WIN32)
    job->thread = CreateThread(NULL, 0, worker_main, job, 0, NULL);
    return job->thread != NULL;
#else
    return pthread_create(&job->thread, NULL, worker_main, job) == 0;
#endif
}

static void join_thread(AiWorkerJob *job)
{
#if defined(_WIN32)
    if (job->thread != NULL) {
        WaitForSingleObject(job->thread, INFINITE);
        CloseHandle(job->thread);
        job->thread = NULL;
    }
#else
    (void)pthread_join(job->thread, NULL);
#endif
}

AiWorker *ai_worker_create(void)
{
    return calloc(1, sizeof(AiWorker));
}

void ai_worker_destroy(AiWorker *worker)
{
    if (worker == NULL) {
        return;
    }
    ai_worker_join(worker);
    free(worker);
}

static bool start_job(AiWorker *worker, const GameState *state,
                      const AiConfig *config, int max_lines,
                      AiWorkerKind kind)
{
    if (worker == NULL || state == NULL || config == NULL
        || worker->job != NULL) {
        return false;
    }

    AiWorkerJob *job = calloc(1, sizeof(AiWorkerJob));
    if (job == NULL) {
        return false;
    }

    job->kind = kind;
    job->state = *state;
    job->config = *config;
    job->config.should_cancel = job_should_cancel;
    job->config.cancel_context = job;
    job->max_lines = max_lines;
    atomic_init(&job->cancel, false);
    atomic_init(&job->done, false);

    if (!start_thread(job)) {
        free(job);
        return false;
    }

    worker->job = job;
    return true;
}

bool ai_worker_start(AiWorker *worker, const GameState *state,
                     const AiConfig *config)
{
    return start_job(worker, state, config, 0, AI_WORKER_JOB_MOVE);
}

AiWorkerStatus ai_worker_status(AiWorker *worker)
{
    if (worker == NULL || worker->job == NULL) {
        return AI_WORKER_IDLE;
    }
    AiWorkerJob *job = worker->job;
    if (atomic_load_explicit(&job->cancel, memory_order_acquire)) {
        return AI_WORKER_FAILED;
    }
    if (!atomic_load_explicit(&job->done, memory_order_acquire)) {
        return AI_WORKER_RUNNING;
    }
    return job->found ? AI_WORKER_READY : AI_WORKER_FAILED;
}

bool ai_worker_start_analysis(AiWorker *worker, const GameState *state,
                              const AiConfig *config, int max_lines)
{
    return start_job(worker, state, config, max_lines,
                     AI_WORKER_JOB_ANALYSIS);
}

bool ai_worker_take(AiWorker *worker, Move *out_move, AiStats *out_stats)
{
    if (worker == NULL || worker->job == NULL || out_move == NULL) {
        return false;
    }
    AiWorkerJob *job = worker->job;
    if (job->kind != AI_WORKER_JOB_MOVE) {
        return false;
    }
    const bool cancelled = atomic_load_explicit(
        &job->cancel, memory_order_acquire);
    if (!cancelled
        && !atomic_load_explicit(&job->done, memory_order_acquire)) {
        return false;
    }

    atomic_store_explicit(&job->cancel, true, memory_order_release);
    join_thread(job);
    const bool found = job->found && !cancelled;
    if (found) {
        *out_move = job->move;
    }
    if (out_stats != NULL) {
        *out_stats = job->stats;
    }
    free(job);
    worker->job = NULL;
    return found;
}

bool ai_worker_take_analysis(AiWorker *worker, AiAnalysis *out_analysis)
{
    if (worker == NULL || worker->job == NULL || out_analysis == NULL) {
        return false;
    }
    AiWorkerJob *job = worker->job;
    if (job->kind != AI_WORKER_JOB_ANALYSIS) {
        return false;
    }
    const bool cancelled = atomic_load_explicit(
        &job->cancel, memory_order_acquire);
    if (!cancelled
        && !atomic_load_explicit(&job->done, memory_order_acquire)) {
        return false;
    }

    atomic_store_explicit(&job->cancel, true, memory_order_release);
    join_thread(job);
    const bool found = job->found && !cancelled;
    if (found) {
        *out_analysis = job->analysis;
    }
    free(job);
    worker->job = NULL;
    return found;
}

bool ai_worker_is_active(const AiWorker *worker)
{
    return worker != NULL && worker->job != NULL;
}

void ai_worker_cancel(AiWorker *worker)
{
    if (worker != NULL && worker->job != NULL) {
        atomic_store_explicit(&worker->job->cancel, true,
                              memory_order_release);
    }
}

void ai_worker_join(AiWorker *worker)
{
    if (worker == NULL || worker->job == NULL) {
        return;
    }
    AiWorkerJob *job = worker->job;
    atomic_store_explicit(&job->cancel, true, memory_order_release);
    join_thread(job);
    free(job);
    worker->job = NULL;
}
