#include "core/match.h"

#include "core/rules.h"

#include <stddef.h>

static bool config_is_valid(const MatchConfig *config)
{
    if (config == NULL) {
        return false;
    }
    if (config->mode != GAME_MODE_PVP && config->mode != GAME_MODE_PVE) {
        return false;
    }
    if (!stone_is_player(config->human_color)) {
        return false;
    }
    if (config->mode == GAME_MODE_PVE
        && (config->ai == NULL || config->ai->select_move == NULL)) {
        return false;
    }
    return true;
}

static bool states_equal(const GameState *a, const GameState *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->current_player != b->current_player || a->result != b->result) {
        return false;
    }
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board_get(&a->board, row, col)
                != board_get(&b->board, row, col)) {
                return false;
            }
        }
    }
    return true;
}

static bool match_initialize(Match *match, const MatchConfig *config,
                             const GameState *initial)
{
    if (match == NULL || !config_is_valid(config) || initial == NULL
        || !game_is_consistent(initial)) {
        return false;
    }
    if (initial->result == GAME_RESULT_ONGOING
        && game_must_pass(initial)) {
        return false;
    }

    *match = (Match){0};
    match->config = *config;
    match->state = *initial;
    match->phase = initial->result == GAME_RESULT_ONGOING
        ? MATCH_PHASE_PLAYER_TURN : MATCH_PHASE_GAME_OVER;
    match->last_move = (Move){0, 0};
    match->last_mover = STONE_EMPTY;
    match->last_flips = 0;
    match->has_last_move = false;
    match->last_pass = (PassEvent){false, STONE_EMPTY};
    history_init(&match->history, initial);
    return true;
}

bool match_start(Match *match, const MatchConfig *config)
{
    GameState initial;
    game_init(&initial);
    return match_initialize(match, config, &initial);
}

bool match_start_from(Match *match, const MatchConfig *config,
                      const GameState *initial)
{
    return match_initialize(match, config, initial);
}

static int count_flips(const Board *before, const Board *after,
                       Stone mover, Move move)
{
    const Stone opponent = stone_opponent(mover);
    int flips = 0;

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (row == move.row && col == move.col) {
                continue;
            }
            if (board_get(before, row, col) == opponent
                && board_get(after, row, col) == mover) {
                ++flips;
            }
        }
    }
    return flips;
}

bool match_apply_move(Match *match, Move move)
{
    if (match == NULL || match->phase != MATCH_PHASE_PLAYER_TURN
        || match->state.result != GAME_RESULT_ONGOING) {
        return false;
    }

    const GameState before = match->state;
    const Stone mover = match->state.current_player;
    if (!stone_is_player(mover)) {
        return false;
    }

    if (!game_apply_move(&match->state, move)) {
        return false;
    }

    const int flips = count_flips(&before.board, &match->state.board,
                                  mover, move);
    const Stone next = stone_opponent(mover);
    PassEvent pass = {false, STONE_EMPTY};
    if (match->state.result == GAME_RESULT_ONGOING
        && !board_is_full(&match->state.board)
        && rules_must_pass(&match->state.board, next)) {
        pass.occurred = true;
        pass.player = next;
    }

    if (!history_record(&match->history, &match->state, move, mover,
                        flips, pass)) {
        match->state = before;
        return false;
    }

    match->last_move = move;
    match->last_mover = mover;
    match->last_flips = flips;
    match->has_last_move = true;
    match->last_pass = pass;
    match->phase = MATCH_PHASE_ANIMATING;
    return true;
}

bool match_finish_animation(Match *match)
{
    if (match == NULL || match->phase != MATCH_PHASE_ANIMATING) {
        return false;
    }

    if (match->state.result != GAME_RESULT_ONGOING) {
        match->phase = MATCH_PHASE_GAME_OVER;
    } else if (match->last_pass.occurred) {
        match->phase = MATCH_PHASE_PASSING_TURN;
    } else {
        match->phase = MATCH_PHASE_PLAYER_TURN;
    }
    return true;
}

bool match_finish_pass(Match *match)
{
    if (match == NULL || match->phase != MATCH_PHASE_PASSING_TURN) {
        return false;
    }

    match->phase = match->state.result == GAME_RESULT_ONGOING
        ? MATCH_PHASE_PLAYER_TURN : MATCH_PHASE_GAME_OVER;
    return true;
}

bool match_pause(Match *match)
{
    if (match == NULL || match->phase != MATCH_PHASE_PLAYER_TURN) {
        return false;
    }

    match->phase = MATCH_PHASE_PAUSED;
    return true;
}

bool match_resume(Match *match)
{
    if (match == NULL || match->phase != MATCH_PHASE_PAUSED) {
        return false;
    }

    match->phase = MATCH_PHASE_PLAYER_TURN;
    return true;
}

static bool phase_allows_history(const Match *match)
{
    return match != NULL
        && (match->phase == MATCH_PHASE_PLAYER_TURN
            || match->phase == MATCH_PHASE_GAME_OVER);
}

static bool pve_has_prior_human_decision(const Match *match)
{
    for (int i = match->history.current - 1; i >= 0; --i) {
        const HistoryEntry *entry = history_entry(&match->history, i);
        if (entry != NULL && entry->state.result == GAME_RESULT_ONGOING
            && entry->state.current_player == match->config.human_color) {
            return true;
        }
    }
    return false;
}

static bool restore_current(Match *match)
{
    const HistoryEntry *entry = history_current(&match->history);
    if (entry == NULL) {
        return false;
    }

    match->state = entry->state;
    match->last_move = entry->move;
    match->last_mover = entry->mover;
    match->last_flips = entry->flips;
    match->has_last_move = entry->has_move;
    match->last_pass = entry->pass;
    match->phase = entry->state.result == GAME_RESULT_ONGOING
        ? MATCH_PHASE_PLAYER_TURN : MATCH_PHASE_GAME_OVER;
    return true;
}

static bool travel_history(Match *match, bool forward)
{
    if (!phase_allows_history(match)) {
        return false;
    }
    if (forward ? !history_can_redo(&match->history)
                : !history_can_undo(&match->history)) {
        return false;
    }
    if (!forward && match->config.mode == GAME_MODE_PVE
        && !pve_has_prior_human_decision(match)) {
        return false;
    }

    const bool pvp = match->config.mode == GAME_MODE_PVP;
    do {
        const bool moved = forward
            ? history_redo(&match->history)
            : history_undo(&match->history);
        if (!moved) {
            return false;
        }

        const HistoryEntry *entry = history_current(&match->history);
        if (entry == NULL || pvp
            || entry->state.result != GAME_RESULT_ONGOING
            || entry->state.current_player == match->config.human_color) {
            break;
        }
    } while (forward ? history_can_redo(&match->history)
                     : history_can_undo(&match->history));

    return restore_current(match);
}

bool match_undo(Match *match)
{
    return travel_history(match, false);
}

bool match_redo(Match *match)
{
    return travel_history(match, true);
}

bool match_restart(Match *match)
{
    if (match == NULL) {
        return false;
    }
    if (match->phase != MATCH_PHASE_PLAYER_TURN
        && match->phase != MATCH_PHASE_PAUSED
        && match->phase != MATCH_PHASE_GAME_OVER) {
        return false;
    }

    const MatchConfig config = match->config;
    GameState initial;
    game_init(&initial);
    return match_initialize(match, &config, &initial);
}

bool match_is_active(const Match *match)
{
    return match != NULL && match->phase != MATCH_PHASE_GAME_OVER;
}

bool match_is_human_turn(const Match *match)
{
    if (match == NULL || match->phase != MATCH_PHASE_PLAYER_TURN) {
        return false;
    }
    if (match->config.mode == GAME_MODE_PVP) {
        return true;
    }
    return match->state.current_player == match->config.human_color;
}

bool match_can_undo(const Match *match)
{
    if (!phase_allows_history(match) || !history_can_undo(&match->history)) {
        return false;
    }
    if (match->config.mode == GAME_MODE_PVE) {
        return pve_has_prior_human_decision(match);
    }
    return true;
}

bool match_can_redo(const Match *match)
{
    return phase_allows_history(match) && history_can_redo(&match->history);
}

bool match_is_consistent(const Match *match)
{
    if (match == NULL || !config_is_valid(&match->config)
        || !game_is_consistent(&match->state)) {
        return false;
    }
    if (match->history.count <= 0 || match->history.current < 0
        || match->history.current >= match->history.count) {
        return false;
    }

    const HistoryEntry *entry = history_current(&match->history);
    if (entry == NULL || !states_equal(&entry->state, &match->state)) {
        return false;
    }
    if (entry->has_move != match->has_last_move
        || entry->move.row != match->last_move.row
        || entry->move.col != match->last_move.col
        || entry->mover != match->last_mover
        || entry->flips != match->last_flips
        || entry->pass.occurred != match->last_pass.occurred
        || entry->pass.player != match->last_pass.player) {
        return false;
    }

    switch (match->phase) {
    case MATCH_PHASE_PLAYER_TURN:
        return match->state.result == GAME_RESULT_ONGOING
            && !game_must_pass(&match->state);
    case MATCH_PHASE_ANIMATING:
        return match->has_last_move;
    case MATCH_PHASE_PASSING_TURN:
        return match->state.result == GAME_RESULT_ONGOING
            && match->last_pass.occurred;
    case MATCH_PHASE_PAUSED:
        return match->state.result == GAME_RESULT_ONGOING;
    case MATCH_PHASE_GAME_OVER:
        return match->state.result != GAME_RESULT_ONGOING;
    default:
        return false;
    }
}
