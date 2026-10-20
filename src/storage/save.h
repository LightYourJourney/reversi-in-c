#ifndef REVERSI_STORAGE_SAVE_H
#define REVERSI_STORAGE_SAVE_H

#include "core/history.h"
#include "core/match.h"

#include <stdbool.h>

#define REVERSI_SAVE_FORMAT_VERSION 1
#define REVERSI_SAVE_MAX_MOVES REVERSI_HISTORY_MAX_MOVES

typedef enum SaveStatus {
    SAVE_STATUS_OK = 0,
    SAVE_STATUS_NOT_FOUND,
    SAVE_STATUS_IO,
    SAVE_STATUS_FORMAT,
    SAVE_STATUS_VERSION,
    SAVE_STATUS_CHECKSUM,
    SAVE_STATUS_STATE
} SaveStatus;

typedef struct SavedGame {
    GameMode mode;
    Stone human_color;
    /* 0 = Easy, 1 = Medium, 2 = Hard (kept as an int so storage does not
       depend on the AI library). */
    int difficulty;
    Move moves[REVERSI_SAVE_MAX_MOVES];
    int move_count;
    GameState expected;
} SavedGame;

/* English status text for the UI. */
const char *save_status_message(SaveStatus status);

/*
 * Writes a complete match. The move list is the current history branch, so
 * undo/redo state is preserved exactly; loading always resumes at a decision
 * point. Any consistent match phase may be saved.
 */
SaveStatus save_write(const char *path, const Match *match, int difficulty);

/*
 * Reads and strictly validates a save. The move sequence is replayed through
 * the rules; malformed coordinates, illegal moves, inconsistent board/player/
 * result, bad checksums, and unsupported versions are rejected without
 * touching *out.
 */
SaveStatus save_read(const char *path, SavedGame *out);

#endif
