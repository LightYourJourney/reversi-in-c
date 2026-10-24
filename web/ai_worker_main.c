/*
 * Entry point for the browser AI worker module.
 *
 * The module links the same core and AI sources as the desktop build; this
 * file only translates the text request built by ai_worker_web.c into a
 * GameState/AiConfig and formats the reply. It does not link raylib.
 *
 * Move request:
 *   "<difficulty> <budget_ms> <seed> <player> <result> <64 cells>"
 *   reply "ok <row> <col> <nodes> <depth> <cancelled>" | "err"
 *
 * Analysis request:
 *   "A <difficulty> <depth> <budget_ms> <seed> <player> <result>
 *      <max_lines> <64 cells>"
 *   reply "ok <depth> <complete> <exact> <lines> <nodes> <elapsed>
 *          <cancelled> <timed_out>" followed per line by
 *          "<row> <col> <score> <depth> <complete> <exact> <pvlen>"
 *          and the PV coordinate pairs.
 */
#include "ai/ai.h"
#include "ai/ai_analyze.h"
#include "core/board.h"
#include "core/game.h"

#include <emscripten.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_reply[4096];

const char *reversi_ai_worker_request(const char *request);

static const char *fail(void)
{
    snprintf(g_reply, sizeof(g_reply), "err");
    return g_reply;
}

static const char *analysis_request(const char *request)
{
    int difficulty = -1;
    int depth = 0;
    int budget_ms = 0;
    unsigned seed = 0;
    int player = 0;
    int result = -1;
    int max_lines = 0;
    char cells[REVERSI_BOARD_CELL_COUNT + 1];

    const int fields = sscanf(request, "%d %d %d %u %d %d %d %64s",
                              &difficulty, &depth, &budget_ms, &seed,
                              &player, &result, &max_lines, cells);
    if (fields != 8
        || strlen(cells) != REVERSI_BOARD_CELL_COUNT
        || difficulty < AI_DIFFICULTY_EASY
        || difficulty > AI_DIFFICULTY_HARD
        || depth < 1 || depth > 32
        || budget_ms < 1 || budget_ms > 10000
        || player < STONE_BLACK || player > STONE_WHITE
        || result < GAME_RESULT_ONGOING || result > GAME_RESULT_DRAW
        || max_lines < 1 || max_lines > REVERSI_AI_ANALYSIS_MAX_LINES) {
        return fail();
    }

    GameState state;
    memset(&state, 0, sizeof(state));
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        const char cell = cells[index];
        if (cell < '0' || cell > '2') {
            return fail();
        }
        state.board.cells[index / REVERSI_BOARD_SIZE]
                         [index % REVERSI_BOARD_SIZE] = (Stone)(cell - '0');
    }
    state.current_player = (Stone)player;
    state.result = (GameResult)result;
    if (!board_is_valid(&state.board)) {
        return fail();
    }

    AiConfig config = ai_default_config((AiDifficulty)difficulty, seed);
    config.max_depth = depth;
    config.time_budget_ms = budget_ms;
    if (!ai_config_is_valid(&config)) {
        return fail();
    }

    AiAnalysis analysis;
    if (!ai_analyze_position(&state, &config, max_lines, &analysis)) {
        return fail();
    }

    int used = snprintf(
        g_reply, sizeof(g_reply), "ok %d %d %d %d %" PRIu64 " %d %d %d",
        analysis.depth, analysis.complete ? 1 : 0, analysis.exact ? 1 : 0,
        analysis.line_count, analysis.stats.nodes,
        analysis.stats.elapsed_ms, analysis.stats.cancelled ? 1 : 0,
        analysis.stats.timed_out ? 1 : 0);
    if (used < 0 || (size_t)used >= sizeof(g_reply)) {
        return fail();
    }

    for (int i = 0; i < analysis.line_count; ++i) {
        const AiAnalysisLine *line = &analysis.lines[i];
        const int remaining = (int)sizeof(g_reply) - used;
        const int wrote = snprintf(
            g_reply + used, (size_t)remaining, " %d %d %d %d %d %d %d",
            line->move.row, line->move.col, line->score, line->depth,
            line->complete ? 1 : 0, line->exact ? 1 : 0, line->pv_length);
        if (wrote < 0 || wrote >= remaining) {
            return fail();
        }
        used += wrote;
        for (int j = 0; j < line->pv_length; ++j) {
            const int left = (int)sizeof(g_reply) - used;
            const int pv_wrote = snprintf(g_reply + used, (size_t)left,
                                          " %d %d", line->pv[j].row,
                                          line->pv[j].col);
            if (pv_wrote < 0 || pv_wrote >= left) {
                return fail();
            }
            used += pv_wrote;
        }
    }
    return g_reply;
}

EMSCRIPTEN_KEEPALIVE
const char *reversi_ai_worker_request(const char *request)
{
    if (request != NULL && request[0] == 'A' && request[1] == ' ') {
        return analysis_request(request + 2);
    }

    int difficulty = -1;
    int budget_ms = 0;
    unsigned seed = 0;
    int player = 0;
    int result = -1;
    char cells[REVERSI_BOARD_CELL_COUNT + 1];

    if (request == NULL) {
        return fail();
    }
    const int fields = sscanf(request, "%d %d %u %d %d %64s",
                              &difficulty, &budget_ms, &seed, &player,
                              &result, cells);
    if (fields != 6
        || strlen(cells) != REVERSI_BOARD_CELL_COUNT
        || difficulty < AI_DIFFICULTY_EASY
        || difficulty > AI_DIFFICULTY_HARD
        || player < STONE_BLACK || player > STONE_WHITE
        || result < GAME_RESULT_ONGOING || result > GAME_RESULT_DRAW
        || budget_ms < 1 || budget_ms > 10000) {
        return fail();
    }

    GameState state;
    memset(&state, 0, sizeof(state));
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        const char cell = cells[index];
        if (cell < '0' || cell > '2') {
            return fail();
        }
        state.board.cells[index / REVERSI_BOARD_SIZE]
                         [index % REVERSI_BOARD_SIZE] = (Stone)(cell - '0');
    }
    state.current_player = (Stone)player;
    state.result = (GameResult)result;
    if (!board_is_valid(&state.board)) {
        return fail();
    }

    AiConfig config = ai_default_config((AiDifficulty)difficulty, seed);
    config.time_budget_ms = budget_ms;
    if (!ai_config_is_valid(&config)) {
        return fail();
    }

    Move move = {-1, -1};
    AiStats stats;
    memset(&stats, 0, sizeof(stats));
    const bool found = ai_choose_move(&state, &config, &move, &stats);
    if (!found) {
        snprintf(g_reply, sizeof(g_reply), "ok -1 -1 %" PRIu64 " %d 1",
                 stats.nodes, stats.depth);
        return g_reply;
    }
    snprintf(g_reply, sizeof(g_reply), "ok %d %d %" PRIu64 " %d %d",
             move.row, move.col, stats.nodes, stats.depth,
             stats.cancelled ? 1 : 0);
    return g_reply;
}
