#include "core/history.h"

#include <stddef.h>

static bool history_index_valid(const History *history, int index)
{
    return history != NULL && index >= 0 && index < history->count;
}

void history_init(History *history, const GameState *initial)
{
    if (history == NULL) {
        return;
    }

    *history = (History){0};
    if (initial == NULL) {
        history->current = -1;
        return;
    }

    history->entries[0].state = *initial;
    history->entries[0].has_move = false;
    history->entries[0].mover = STONE_EMPTY;
    history->entries[0].move = (Move){0, 0};
    history->entries[0].flips = 0;
    history->entries[0].pass = (PassEvent){false, STONE_EMPTY};
    history->count = 1;
    history->current = 0;
}

bool history_record(History *history, const GameState *state,
                    Move move, Stone mover, int flips, PassEvent pass)
{
    if (history == NULL || state == NULL || history->count <= 0
        || !history_index_valid(history, history->current)) {
        return false;
    }
    if (history->current + 1 >= REVERSI_HISTORY_MAX_ENTRIES) {
        return false;
    }

    const int next = history->current + 1;
    HistoryEntry *entry = &history->entries[next];
    entry->state = *state;
    entry->move = move;
    entry->mover = mover;
    entry->flips = flips;
    entry->has_move = true;
    entry->pass = pass;
    if (!entry->pass.occurred) {
        entry->pass.player = STONE_EMPTY;
    }

    history->current = next;
    history->count = next + 1;
    return true;
}

bool history_can_undo(const History *history)
{
    return history != NULL && history->count > 0
        && history->current > 0;
}

bool history_can_redo(const History *history)
{
    return history != NULL && history->count > 0
        && history->current + 1 < history->count;
}

bool history_undo(History *history)
{
    if (!history_can_undo(history)) {
        return false;
    }

    --history->current;
    return true;
}

bool history_redo(History *history)
{
    if (!history_can_redo(history)) {
        return false;
    }

    ++history->current;
    return true;
}

const HistoryEntry *history_current(const History *history)
{
    if (history == NULL || !history_index_valid(history, history->current)) {
        return NULL;
    }
    return &history->entries[history->current];
}

const HistoryEntry *history_entry(const History *history, int index)
{
    if (!history_index_valid(history, index)) {
        return NULL;
    }
    return &history->entries[index];
}

int history_count(const History *history)
{
    return history != NULL ? history->count : 0;
}

int history_position(const History *history)
{
    return history != NULL ? history->current : -1;
}
