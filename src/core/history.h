#ifndef REVERSI_CORE_HISTORY_H
#define REVERSI_CORE_HISTORY_H

#include "core/game.h"

#include <stdbool.h>

#define REVERSI_HISTORY_MAX_ENTRIES 65
#define REVERSI_HISTORY_MAX_MOVES (REVERSI_HISTORY_MAX_ENTRIES - 1)

typedef struct PassEvent {
    bool occurred;
    Stone player;
} PassEvent;

typedef struct HistoryEntry {
    GameState state;
    Move move;
    Stone mover;
    int flips;
    bool has_move;
    PassEvent pass;
} HistoryEntry;

typedef struct History {
    HistoryEntry entries[REVERSI_HISTORY_MAX_ENTRIES];
    int count;
    int current;
} History;

/* Initializes a history whose entry 0 is the given starting position. */
void history_init(History *history, const GameState *initial);

/*
 * Appends the position after a move. Any redo branch beyond the current
 * position is discarded. Returns false without modifying the history when
 * it is full or the arguments are invalid.
 */
bool history_record(History *history, const GameState *state,
                    Move move, Stone mover, int flips, PassEvent pass);

bool history_can_undo(const History *history);
bool history_can_redo(const History *history);
bool history_undo(History *history);
bool history_redo(History *history);

const HistoryEntry *history_current(const History *history);
const HistoryEntry *history_entry(const History *history, int index);
int history_count(const History *history);
int history_position(const History *history);

#endif
