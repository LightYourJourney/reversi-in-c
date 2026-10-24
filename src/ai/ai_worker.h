#ifndef REVERSI_AI_WORKER_H
#define REVERSI_AI_WORKER_H

#include "ai/ai.h"
#include "ai/ai_analyze.h"

typedef struct AiWorker AiWorker;

typedef enum AiWorkerStatus {
    AI_WORKER_IDLE = 0,
    AI_WORKER_RUNNING,
    AI_WORKER_READY,
    AI_WORKER_FAILED
} AiWorkerStatus;

AiWorker *ai_worker_create(void);
void ai_worker_destroy(AiWorker *worker);

/*
 * Starts a search on a copy of the state. The worker never touches the
 * caller's state or UI. Returns false if a search is already active or the
 * thread could not be created.
 */
bool ai_worker_start(AiWorker *worker, const GameState *state,
                     const AiConfig *config);

AiWorkerStatus ai_worker_status(AiWorker *worker);

/*
 * Joins a finished search and returns its move. Call only when status is
 * READY or FAILED; returns false for FAILED/cancelled searches. The worker
 * becomes idle afterwards.
 */
bool ai_worker_take(AiWorker *worker, Move *out_move, AiStats *out_stats);

/*
 * MultiPV analysis job. Same lifecycle and cancellation as ai_worker_start(),
 * but the result is taken with ai_worker_take_analysis(). A worker runs one
 * job at a time; starting a job while another is active fails.
 */
bool ai_worker_start_analysis(AiWorker *worker, const GameState *state,
                              const AiConfig *config, int max_lines);

/*
 * Joins a finished analysis job and copies its result. Returns false when the
 * job failed or was cancelled; the worker becomes idle afterwards.
 */
bool ai_worker_take_analysis(AiWorker *worker, AiAnalysis *out_analysis);

bool ai_worker_is_active(const AiWorker *worker);

/* Request cancellation; the search checks it every 1024 nodes. */
void ai_worker_cancel(AiWorker *worker);

/* Cancels, waits for the thread and discards any result. */
void ai_worker_join(AiWorker *worker);

#endif
