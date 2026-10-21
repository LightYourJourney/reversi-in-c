/*
 * Entry point for the browser AI worker module.
 *
 * The module links the same core and AI sources as the desktop build; this
 * file only translates the text request built by ai_worker_web.c into a
 * GameState/AiConfig and formats the reply. It does not link raylib.
 *
 * Request:  "<difficulty> <budget_ms> <seed> <player> <result> <64 cells>"
 * Reply:    "ok <row> <col> <nodes> <depth> <cancelled>" | "err"
 */
#include "ai/ai.h"
#include "core/board.h"
#include "core/game.h"

#include <emscripten.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_reply[128];

const char *reversi_ai_worker_request(const char *request, unsigned generation);

static const char *fail(void)
{
    snprintf(g_reply, sizeof(g_reply), "err");
    return g_reply;
}

EMSCRIPTEN_KEEPALIVE
const char *reversi_ai_worker_request(const char *request, unsigned generation)
{
    (void)generation;

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
