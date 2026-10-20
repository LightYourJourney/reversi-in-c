#include "ai/ai.h"

#include "ai/ai_search.h"
#include "core/rules.h"

#include <stddef.h>

AiConfig ai_default_config(AiDifficulty difficulty, unsigned seed)
{
    AiConfig config = {
        .difficulty = difficulty,
        .max_depth = 4,
        .time_budget_ms = 200,
        .randomness = 0,
        .seed = seed,
        .should_cancel = NULL,
        .cancel_context = NULL
    };

    switch (difficulty) {
    case AI_DIFFICULTY_EASY:
        config.max_depth = 1;
        config.time_budget_ms = 30;
        config.randomness = 40;
        break;
    case AI_DIFFICULTY_HARD:
        config.max_depth = 10;
        config.time_budget_ms = 500;
        break;
    case AI_DIFFICULTY_MEDIUM:
    default:
        break;
    }
    return config;
}

bool ai_config_is_valid(const AiConfig *config)
{
    if (config == NULL) {
        return false;
    }
    if (config->difficulty != AI_DIFFICULTY_EASY
        && config->difficulty != AI_DIFFICULTY_MEDIUM
        && config->difficulty != AI_DIFFICULTY_HARD) {
        return false;
    }
    if (config->max_depth < 1 || config->max_depth > 32) {
        return false;
    }
    if (config->time_budget_ms < 1 || config->time_budget_ms > 10000) {
        return false;
    }
    if (config->randomness < 0 || config->randomness > 1000) {
        return false;
    }
    return true;
}

const char *ai_difficulty_name(AiDifficulty difficulty)
{
    switch (difficulty) {
    case AI_DIFFICULTY_EASY:
        return "Easy";
    case AI_DIFFICULTY_MEDIUM:
        return "Medium";
    case AI_DIFFICULTY_HARD:
        return "Hard";
    default:
        return "Unknown";
    }
}

bool ai_choose_move(const GameState *state, const AiConfig *config,
                    Move *out_move, AiStats *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = (AiStats){0};
    }
    if (state == NULL || out_move == NULL || !ai_config_is_valid(config)
        || state->result != GAME_RESULT_ONGOING
        || !stone_is_player(state->current_player)
        || game_must_pass(state)) {
        return false;
    }

    bool found = false;
    if (config->difficulty == AI_DIFFICULTY_EASY) {
        found = ai_easy_choose(state, config, out_move, out_stats);
    } else {
        found = ai_search_choose(state, config, out_move, out_stats);
    }
    if (!found) {
        return false;
    }

    /* Final guard: the public API never returns an illegal move. */
    if (!rules_is_legal_move(&state->board, state->current_player, *out_move)) {
        return false;
    }
    return true;
}

static bool easy_select(void *context, const GameState *state,
                        Move *out_move)
{
    (void)context;
    const AiConfig config = ai_default_config(AI_DIFFICULTY_EASY, 0);
    return ai_choose_move(state, &config, out_move, NULL);
}

static bool medium_select(void *context, const GameState *state,
                          Move *out_move)
{
    (void)context;
    const AiConfig config = ai_default_config(AI_DIFFICULTY_MEDIUM, 0);
    return ai_choose_move(state, &config, out_move, NULL);
}

static bool hard_select(void *context, const GameState *state,
                        Move *out_move)
{
    (void)context;
    const AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0);
    return ai_choose_move(state, &config, out_move, NULL);
}

/*
 * Synchronous controllers are the Match boundary (MatchConfig.ai). The app's
 * PvE path uses AiWorker + ai_choose_move() with its own config and seed.
 */
static const AiController AI_CONTROLLERS[3] = {
    { "Easy", easy_select, NULL },
    { "Medium", medium_select, NULL },
    { "Hard", hard_select, NULL }
};

const AiController *ai_controller_for_difficulty(AiDifficulty difficulty)
{
    if (difficulty != AI_DIFFICULTY_EASY
        && difficulty != AI_DIFFICULTY_MEDIUM
        && difficulty != AI_DIFFICULTY_HARD) {
        return NULL;
    }
    return &AI_CONTROLLERS[difficulty];
}
