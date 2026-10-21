/*
 * Browser AI worker adapter.
 *
 * Same public contract as the desktop worker (ai_worker.h), but the search
 * runs in a separate Web Worker instead of a pthread. The browser main
 * thread never blocks: results arrive asynchronously and are published into
 * the worker state from a JS callback, which ai_worker_status() polls once
 * per frame. Cancellation terminates the worker, so a stale result can never
 * be observed (the generation counter also guards against it).
 */
#include "ai/ai_worker.h"

#include "core/game.h"

#include <emscripten.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_WEB_BOARD_CHARS 64
#define REVERSI_WEB_REQUEST_CAP 192

struct AiWorker {
    AiWorkerStatus status;
    Move move;
    AiStats stats;
    unsigned generation;
};

static AiWorker *g_active_worker = NULL;
static unsigned g_generation = 0;

void reversi_ai_worker_complete(unsigned generation, int ok, int row, int col,
                                double nodes, int depth, int cancelled);

EM_JS(void, reversi_web_ai_post, (const char *request, unsigned generation), {
    function createWorker() {
        var worker = new Worker('ai_worker_boot.js');
        worker.onmessage = function (event) {
            var d = event.data || {};
            var raw = String(d.raw || 'err').split(' ');
            var ok = raw[0] === 'ok' ? 1 : 0;
            var row = ok ? parseInt(raw[1], 10) : -1;
            var col = ok ? parseInt(raw[2], 10) : -1;
            var nodes = ok ? parseFloat(raw[3]) : 0;
            var depth = ok ? parseInt(raw[4], 10) : 0;
            var cancelled = ok ? parseInt(raw[5], 10) : 1;
            var cb = Module['_reversi_ai_worker_complete'];
            if (cb) {
                cb(Number(d.generation) >>> 0, ok, row, col, nodes, depth,
                   cancelled);
            }
        };
        worker.onerror = function () {
            var generation = Number(
                globalThis.__reversiAiGeneration || 0) >>> 0;
            var cb = Module['_reversi_ai_worker_complete'];
            if (cb) {
                cb(generation, 0, -1, -1, 0, 0, 1);
            }
        };
        return worker;
    }
    if (!globalThis.__reversiAiWorker) {
        globalThis.__reversiAiWorker = createWorker();
    }
    globalThis.__reversiAiGeneration = generation >>> 0;
    globalThis.__reversiAiWorker.postMessage({
        generation: generation >>> 0,
        request: UTF8ToString(request)
    })
})

EM_JS(void, reversi_web_ai_cancel, (void), {
    if (globalThis.__reversiAiWorker) {
        globalThis.__reversiAiWorker.terminate();
        globalThis.__reversiAiWorker = null;
        globalThis.__reversiAiGeneration = 0;
    }
})

EMSCRIPTEN_KEEPALIVE
void reversi_ai_worker_complete(unsigned generation, int ok, int row, int col,
                                double nodes, int depth, int cancelled)
{
    AiWorker *worker = g_active_worker;
    if (worker == NULL || worker->generation != generation) {
        return;
    }
    if (ok) {
        worker->move.row = row;
        worker->move.col = col;
        worker->stats.nodes = (uint64_t)nodes;
        worker->stats.depth = depth;
        worker->stats.cancelled = cancelled != 0;
        worker->status = AI_WORKER_READY;
    } else {
        worker->status = AI_WORKER_FAILED;
    }
    g_active_worker = NULL;
}

AiWorker *ai_worker_create(void)
{
    AiWorker *worker = calloc(1, sizeof(*worker));
    if (worker != NULL) {
        worker->status = AI_WORKER_IDLE;
    }
    return worker;
}

void ai_worker_destroy(AiWorker *worker)
{
    if (worker == NULL) {
        return;
    }
    ai_worker_join(worker);
    free(worker);
}

bool ai_worker_start(AiWorker *worker, const GameState *state,
                     const AiConfig *config)
{
    if (worker == NULL || state == NULL || config == NULL
        || worker->status == AI_WORKER_RUNNING
        || g_active_worker != NULL) {
        return false;
    }

    char board[REVERSI_WEB_BOARD_CHARS + 1];
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            board[row * REVERSI_BOARD_SIZE + col] =
                (char)('0' + (int)state->board.cells[row][col]);
        }
    }
    board[REVERSI_WEB_BOARD_CHARS] = '\0';

    char request[REVERSI_WEB_REQUEST_CAP];
    const int written = snprintf(
        request, sizeof(request), "%d %d %u %d %d %s",
        (int)config->difficulty, config->time_budget_ms, config->seed,
        (int)state->current_player, (int)state->result, board);
    if (written <= 0 || (size_t)written >= sizeof(request)) {
        return false;
    }

    const unsigned generation = ++g_generation;
    worker->generation = generation;
    worker->status = AI_WORKER_RUNNING;
    g_active_worker = worker;
    reversi_web_ai_post(request, generation);
    return true;
}

AiWorkerStatus ai_worker_status(AiWorker *worker)
{
    if (worker == NULL) {
        return AI_WORKER_FAILED;
    }
    return worker->status;
}

bool ai_worker_take(AiWorker *worker, Move *out_move, AiStats *out_stats)
{
    if (worker == NULL) {
        return false;
    }
    if (worker->status == AI_WORKER_READY) {
        if (out_move != NULL) {
            *out_move = worker->move;
        }
        if (out_stats != NULL) {
            *out_stats = worker->stats;
        }
        worker->status = AI_WORKER_IDLE;
        return true;
    }
    if (worker->status == AI_WORKER_FAILED) {
        worker->status = AI_WORKER_IDLE;
    }
    return false;
}

bool ai_worker_is_active(const AiWorker *worker)
{
    return worker != NULL && worker->status == AI_WORKER_RUNNING;
}

void ai_worker_cancel(AiWorker *worker)
{
    if (worker == NULL) {
        return;
    }
    reversi_web_ai_cancel();
    ++g_generation;
    if (g_active_worker == worker) {
        g_active_worker = NULL;
    }
    if (worker->status == AI_WORKER_RUNNING) {
        worker->status = AI_WORKER_FAILED;
    }
}

void ai_worker_join(AiWorker *worker)
{
    if (worker == NULL) {
        return;
    }
    ai_worker_cancel(worker);
    worker->status = AI_WORKER_IDLE;
}
