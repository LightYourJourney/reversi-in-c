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

#define REVERSI_WEB_REQUEST_CAP 192

struct AiWorker {
    AiWorkerStatus status;
    bool analysis_job;
    Move move;
    AiStats stats;
    AiAnalysis analysis;
    unsigned generation;
};

static AiWorker *g_active_worker = NULL;
static unsigned g_generation = 0;

void reversi_ai_worker_complete(unsigned generation, int ok, int row, int col,
                                double nodes, int depth, int cancelled);
void reversi_ai_worker_analysis_complete(unsigned generation, int ok,
                                         const char *text);

EM_JS(void, reversi_web_ai_post, (const char *request, unsigned generation), {
    function createWorker() {
        var worker = new Worker('ai_worker_boot.js');
        worker.onmessage = function (event) {
            var d = event.data || {};
            var rawText = String(d.raw || 'err');
            var raw = rawText.split(' ');
            var ok = raw[0] === 'ok' ? 1 : 0;
            var generation = Number(d.generation) >>> 0;
            if (d.kind === 'analysis') {
                var acb = Module['_reversi_ai_worker_analysis_complete'];
                if (acb) {
                    Module.ccall('reversi_ai_worker_analysis_complete',
                        null, ['number', 'number', 'string'],
                        [generation, ok, rawText]);
                }
                return;
            }
            var row = ok ? parseInt(raw[1], 10) : -1;
            var col = ok ? parseInt(raw[2], 10) : -1;
            var nodes = ok ? parseFloat(raw[3], 10) : 0;
            var depth = ok ? parseInt(raw[4], 10) : 0;
            var cancelled = ok ? parseInt(raw[5], 10) : 1;
            var cb = Module['_reversi_ai_worker_complete'];
            if (cb) {
                cb(generation, ok, row, col, nodes, depth, cancelled);
            }
        };
        worker.onerror = function () {
            var generation = Number(
                globalThis.__reversiAiGeneration || 0) >>> 0;
            if (globalThis.__reversiAiKind === 'analysis') {
                var acb = Module['_reversi_ai_worker_analysis_complete'];
                if (acb) {
                    Module.ccall('reversi_ai_worker_analysis_complete',
                        null, ['number', 'number', 'string'],
                        [generation, 0, 'err']);
                }
                return;
            }
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
    globalThis.__reversiAiKind =
        UTF8ToString(request).charAt(0) === 'A' ? 'analysis' : 'move';
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

static bool take_long(const char **cursor, long *out)
{
    while (**cursor == ' ') {
        ++*cursor;
    }
    if (**cursor == '\0') {
        return false;
    }
    char *end = NULL;
    const long value = strtol(*cursor, &end, 10);
    if (end == *cursor) {
        return false;
    }
    *cursor = end;
    *out = value;
    return true;
}

static bool take_u64(const char **cursor, uint64_t *out)
{
    while (**cursor == ' ') {
        ++*cursor;
    }
    if (**cursor == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long long value = strtoull(*cursor, &end, 10);
    if (end == *cursor) {
        return false;
    }
    *cursor = end;
    *out = (uint64_t)value;
    return true;
}

static bool parse_analysis(const char *text, AiAnalysis *out)
{
    const char *cursor = text;
    while (*cursor == ' ') {
        ++cursor;
    }
    if (strncmp(cursor, "ok", 2) != 0
        || (cursor[2] != ' ' && cursor[2] != '\0')) {
        return false;
    }
    cursor += 2;

    long depth = 0;
    long complete = 0;
    long exact = 0;
    long line_count = 0;
    uint64_t nodes = 0;
    long elapsed = 0;
    long cancelled = 0;
    long timed_out = 0;
    if (!take_long(&cursor, &depth) || !take_long(&cursor, &complete)
        || !take_long(&cursor, &exact) || !take_long(&cursor, &line_count)
        || !take_u64(&cursor, &nodes) || !take_long(&cursor, &elapsed)
        || !take_long(&cursor, &cancelled)
        || !take_long(&cursor, &timed_out)) {
        return false;
    }
    if (depth < 0 || depth > 64 || line_count < 0
        || line_count > AI_ANALYSIS_MAX_LINES) {
        return false;
    }

    AiAnalysis analysis;
    memset(&analysis, 0, sizeof(analysis));
    analysis.depth = (int)depth;
    analysis.complete = complete != 0;
    analysis.exact = exact != 0;
    analysis.line_count = (int)line_count;
    analysis.stats.nodes = nodes;
    analysis.stats.depth = (int)depth;
    analysis.stats.elapsed_ms = (int)elapsed;
    analysis.stats.cancelled = cancelled != 0;
    analysis.stats.timed_out = timed_out != 0;

    for (int i = 0; i < analysis.line_count; ++i) {
        AiAnalysisLine *line = &analysis.lines[i];
        long row = 0;
        long col = 0;
        long score = 0;
        long line_depth = 0;
        long line_complete = 0;
        long line_exact = 0;
        long pv_length = 0;
        if (!take_long(&cursor, &row) || !take_long(&cursor, &col)
            || !take_long(&cursor, &score)
            || !take_long(&cursor, &line_depth)
            || !take_long(&cursor, &line_complete)
            || !take_long(&cursor, &line_exact)
            || !take_long(&cursor, &pv_length)) {
            return false;
        }
        if (row < 0 || row >= REVERSI_BOARD_SIZE || col < 0
            || col >= REVERSI_BOARD_SIZE || pv_length < 0
            || pv_length > AI_ANALYSIS_MAX_PV) {
            return false;
        }
        line->move.row = (int)row;
        line->move.col = (int)col;
        line->score = (int)score;
        line->depth = (int)line_depth;
        line->complete = line_complete != 0;
        line->exact = line_exact != 0;
        line->pv_length = (int)pv_length;
        for (int j = 0; j < line->pv_length; ++j) {
            long pv_row = 0;
            long pv_col = 0;
            if (!take_long(&cursor, &pv_row)
                || !take_long(&cursor, &pv_col) || pv_row < 0
                || pv_row >= REVERSI_BOARD_SIZE || pv_col < 0
                || pv_col >= REVERSI_BOARD_SIZE) {
                return false;
            }
            line->pv[j].row = (int)pv_row;
            line->pv[j].col = (int)pv_col;
        }
    }

    *out = analysis;
    return true;
}

EMSCRIPTEN_KEEPALIVE
void reversi_ai_worker_analysis_complete(unsigned generation, int ok,
                                         const char *text)
{
    AiWorker *worker = g_active_worker;
    if (worker == NULL || worker->generation != generation
        || !worker->analysis_job) {
        return;
    }
    const bool parsed = ok != 0 && text != NULL
        && parse_analysis(text, &worker->analysis);
    worker->status = parsed ? AI_WORKER_READY : AI_WORKER_FAILED;
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

    char board[REVERSI_BOARD_CELL_COUNT + 1];
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            board[row * REVERSI_BOARD_SIZE + col] =
                (char)('0' + (int)state->board.cells[row][col]);
        }
    }
    board[REVERSI_BOARD_CELL_COUNT] = '\0';

    char request[REVERSI_WEB_REQUEST_CAP];
    const int written = snprintf(
        request, sizeof(request), "%d %d %u %d %d %s",
        (int)config->difficulty, config->time_budget_ms, config->seed,
        (int)state->current_player, (int)state->result, board);
    if (written <= 0 || (size_t)written >= sizeof(request)) {
        return false;
    }

    const unsigned generation = ++g_generation;
    worker->analysis_job = false;
    worker->generation = generation;
    worker->status = AI_WORKER_RUNNING;
    g_active_worker = worker;
    reversi_web_ai_post(request, generation);
    return true;
}

bool ai_worker_start_analysis(AiWorker *worker, const GameState *state,
                              const AiConfig *config, int max_lines)
{
    if (worker == NULL || state == NULL || config == NULL
        || !ai_config_is_valid(config)
        || worker->status == AI_WORKER_RUNNING
        || g_active_worker != NULL) {
        return false;
    }
    if (max_lines < 1) {
        max_lines = 1;
    }
    if (max_lines > AI_ANALYSIS_MAX_LINES) {
        max_lines = AI_ANALYSIS_MAX_LINES;
    }

    char board[REVERSI_BOARD_CELL_COUNT + 1];
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            board[row * REVERSI_BOARD_SIZE + col] =
                (char)('0' + (int)state->board.cells[row][col]);
        }
    }
    board[REVERSI_BOARD_CELL_COUNT] = '\0';

    char request[REVERSI_WEB_REQUEST_CAP];
    const int written = snprintf(
        request, sizeof(request), "A %d %d %d %u %d %d %d %s",
        (int)config->difficulty, config->max_depth, config->time_budget_ms,
        config->seed, (int)state->current_player, (int)state->result,
        max_lines, board);
    if (written <= 0 || (size_t)written >= sizeof(request)) {
        return false;
    }

    const unsigned generation = ++g_generation;
    worker->analysis_job = true;
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
    if (worker == NULL || worker->analysis_job) {
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

bool ai_worker_take_analysis(AiWorker *worker, AiAnalysis *out_analysis)
{
    if (worker == NULL || out_analysis == NULL || !worker->analysis_job) {
        return false;
    }
    if (worker->status == AI_WORKER_READY) {
        *out_analysis = worker->analysis;
        worker->status = AI_WORKER_IDLE;
        worker->analysis_job = false;
        return true;
    }
    if (worker->status == AI_WORKER_FAILED) {
        worker->status = AI_WORKER_IDLE;
        worker->analysis_job = false;
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
