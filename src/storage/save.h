#ifndef REVERSI_STORAGE_SAVE_H
#define REVERSI_STORAGE_SAVE_H

#include "core/history.h"
#include "core/match.h"

#include <stdbool.h>
#include <stddef.h>

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

/* What the autosave should do after a match changes. */
typedef enum SaveAutosaveAction {
    SAVE_AUTOSAVE_SKIP = 0, /* leave the autosave file alone */
    SAVE_AUTOSAVE_REMOVE,   /* the game is over: nothing left to resume */
    SAVE_AUTOSAVE_WRITE     /* overwrite it with the current match */
} SaveAutosaveAction;

/* An online match mirrors the server (often from a mid-game root), so it
 * neither overwrites nor deletes the local game's autosave. */
SaveAutosaveAction save_autosave_action(const Match *match, bool online);

/* One-line description for the save list ("PvE / Medium / 12 moves",
 * " / finished" appended once the game is decided). `difficulty_name` is
 * only used for PvE; the storage layer stays independent of the AI library,
 * so the caller supplies the name. Writes an empty string on bad input. */
void save_slot_summary(const SavedGame *saved, const char *difficulty_name,
                       char *out, size_t cap);

/* English status text for the UI. */
const char *save_status_message(SaveStatus status);

/*
 * Writes a match that started from the standard opening (any other root is
 * refused with SAVE_STATUS_STATE, since loading replays from the opening).
 * The move list ends at the current position: moves undone but not redone
 * are not saved, and loading resumes at a decision point. Any consistent
 * match phase may be saved.
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
