#include "storage/save.h"

#include "core/game.h"
#include "storage/storage.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_SAVE_MAX_FILE_BYTES 65536u
#define REVERSI_SAVE_PAYLOAD_CAP 8192u

const char *save_status_message(SaveStatus status)
{
    switch (status) {
    case SAVE_STATUS_OK:
        return "Saved";
    case SAVE_STATUS_NOT_FOUND:
        return "Save not found";
    case SAVE_STATUS_VERSION:
        return "Save version is not supported";
    case SAVE_STATUS_CHECKSUM:
        return "Save checksum is invalid";
    case SAVE_STATUS_STATE:
        return "Save data is inconsistent";
    case SAVE_STATUS_FORMAT:
        return "Save file is corrupt";
    case SAVE_STATUS_IO:
    default:
        return "Could not access the save file";
    }
}

static unsigned long checksum_text(const char *text)
{
    unsigned long hash = 2166136261UL;
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; ++cursor) {
        hash ^= (unsigned long)*cursor;
        hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
    }
    return hash;
}

typedef struct TextBuilder {
    char *data;
    size_t cap;
    size_t length;
    bool ok;
} TextBuilder;

static void append_text(TextBuilder *builder, const char *format, ...)
{
    if (!builder->ok || builder->length >= builder->cap) {
        builder->ok = false;
        return;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(builder->data + builder->length,
                                  builder->cap - builder->length,
                                  format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= builder->cap - builder->length) {
        builder->ok = false;
        return;
    }
    builder->length += (size_t)written;
}

static char stone_letter(Stone stone)
{
    switch (stone) {
    case STONE_BLACK:
        return 'B';
    case STONE_WHITE:
        return 'W';
    case STONE_EMPTY:
    default:
        return '.';
    }
}

SaveStatus save_write(const char *path, const Match *match, int difficulty)
{
    if (path == NULL || path[0] == '\0' || match == NULL) {
        return SAVE_STATUS_IO;
    }
    if (difficulty < 0 || difficulty > 2) {
        return SAVE_STATUS_STATE;
    }
    if (!match_is_consistent(match)) {
        return SAVE_STATUS_STATE;
    }
    const History *history = &match->history;
    if (history->current < 0
        || history->current > REVERSI_SAVE_MAX_MOVES) {
        return SAVE_STATUS_STATE;
    }

    char payload[REVERSI_SAVE_PAYLOAD_CAP];
    TextBuilder builder = {
        .data = payload,
        .cap = sizeof(payload),
        .length = 0,
        .ok = true
    };

    append_text(&builder, "mode %d\n", (int)match->config.mode);
    append_text(&builder, "human %d\n", (int)match->config.human_color);
    append_text(&builder, "difficulty %d\n", difficulty);
    append_text(&builder, "moves %d\n", history->current);
    for (int index = 1; index <= history->current; ++index) {
        const HistoryEntry *entry = history_entry(history, index);
        if (entry == NULL || !entry->has_move
            || !board_in_bounds(entry->move.row, entry->move.col)) {
            return SAVE_STATUS_STATE;
        }
        append_text(&builder, "move %d %d\n",
                    entry->move.row, entry->move.col);
    }

    append_text(&builder, "board ");
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            append_text(&builder, "%c",
                        stone_letter(board_get(&match->state.board, row, col)));
        }
    }
    append_text(&builder, "\nplayer %d\n", (int)match->state.current_player);
    append_text(&builder, "result %d\n", (int)match->state.result);

    if (!builder.ok) {
        return SAVE_STATUS_STATE;
    }

    char file[REVERSI_SAVE_PAYLOAD_CAP + 128];
    const int written = snprintf(
        file, sizeof(file), "REVERSI_SAVE %d\nchecksum %08lX\n%s",
        REVERSI_SAVE_FORMAT_VERSION, checksum_text(payload), payload);
    if (written <= 0 || (size_t)written >= sizeof(file)) {
        return SAVE_STATUS_STATE;
    }
    return storage_write_atomic(path, file, (size_t)written)
        ? SAVE_STATUS_OK : SAVE_STATUS_IO;
}

static bool split_line(char **cursor, char **line)
{
    if (cursor == NULL || *cursor == NULL || **cursor == '\0') {
        return false;
    }
    char *start = *cursor;
    char *newline = strchr(start, '\n');
    if (newline != NULL) {
        *newline = '\0';
        *cursor = newline + 1;
    } else {
        *cursor = start + strlen(start);
    }
    const size_t length = strlen(start);
    if (length > 0 && start[length - 1] == '\r') {
        start[length - 1] = '\0';
    }
    *line = start;
    return true;
}

static bool parse_int_value(const char *line, const char *key, int *out)
{
    const size_t key_length = strlen(key);
    if (strncmp(line, key, key_length) != 0 || line[key_length] != ' ') {
        return false;
    }
    const char *text = line + key_length + 1;
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < -1000000L || value > 1000000L) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool parse_move_line(const char *line, Move *move)
{
    if (strncmp(line, "move ", 5) != 0) {
        return false;
    }
    const char *text = line + 5;
    char *end = NULL;
    const long row = strtol(text, &end, 10);
    if (end == text || *end != ' ') {
        return false;
    }
    const char *second = end + 1;
    const long col = strtol(second, &end, 10);
    if (end == second || *end != '\0'
        || row < 0 || row >= REVERSI_BOARD_SIZE
        || col < 0 || col >= REVERSI_BOARD_SIZE) {
        return false;
    }
    move->row = (int)row;
    move->col = (int)col;
    return true;
}

static bool parse_board_line(const char *line, Board *board)
{
    if (strncmp(line, "board ", 6) != 0) {
        return false;
    }
    const char *text = line + 6;
    if (strlen(text) != REVERSI_BOARD_CELL_COUNT) {
        return false;
    }
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        Stone stone;
        switch (text[index]) {
        case 'B':
            stone = STONE_BLACK;
            break;
        case 'W':
            stone = STONE_WHITE;
            break;
        case '.':
            stone = STONE_EMPTY;
            break;
        default:
            return false;
        }
        board->cells[index / REVERSI_BOARD_SIZE]
                    [index % REVERSI_BOARD_SIZE] = stone;
    }
    return true;
}

static bool board_matches(const Board *a, const Board *b)
{
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board_get(a, row, col) != board_get(b, row, col)) {
                return false;
            }
        }
    }
    return true;
}

SaveStatus save_read(const char *path, SavedGame *out)
{
    if (path == NULL || path[0] == '\0' || out == NULL) {
        return SAVE_STATUS_IO;
    }

    char *file = NULL;
    size_t file_size = 0;
    if (!storage_read(path, &file, &file_size,
                      REVERSI_SAVE_MAX_FILE_BYTES)) {
        return storage_exists(path) ? SAVE_STATUS_IO : SAVE_STATUS_NOT_FOUND;
    }
    if (file_size == 0 || memchr(file, '\0', file_size) != NULL) {
        free(file);
        return SAVE_STATUS_FORMAT;
    }

    SaveStatus status = SAVE_STATUS_FORMAT;
    SavedGame parsed;
    memset(&parsed, 0, sizeof(parsed));

    char *cursor = file;
    char *line = NULL;
    if (!split_line(&cursor, &line)
        || strncmp(line, "REVERSI_SAVE ", 13) != 0) {
        goto done;
    }
    {
        char *end = NULL;
        const long version = strtol(line + 13, &end, 10);
        if (end == line + 13 || *end != '\0' || version < 1) {
            goto done;
        }
        if (version != REVERSI_SAVE_FORMAT_VERSION) {
            status = SAVE_STATUS_VERSION;
            goto done;
        }
    }

    if (!split_line(&cursor, &line)
        || strncmp(line, "checksum ", 9) != 0) {
        goto done;
    }
    {
        char *end = NULL;
        const unsigned long expected = strtoul(line + 9, &end, 16);
        if (end == line + 9 || *end != '\0' || expected > 0xFFFFFFFFUL) {
            goto done;
        }
        if (checksum_text(cursor) != expected) {
            status = SAVE_STATUS_CHECKSUM;
            goto done;
        }
    }

    int mode = 0;
    int human = 0;
    int difficulty = 0;
    int move_count = 0;
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "mode", &mode)
        || (mode != (int)GAME_MODE_PVP && mode != (int)GAME_MODE_PVE)) {
        goto done;
    }
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "human", &human)
        || (human != (int)STONE_BLACK && human != (int)STONE_WHITE)) {
        goto done;
    }
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "difficulty", &difficulty)
        || difficulty < 0 || difficulty > 2) {
        goto done;
    }
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "moves", &move_count)
        || move_count < 0 || move_count > REVERSI_SAVE_MAX_MOVES) {
        goto done;
    }

    parsed.mode = (GameMode)mode;
    parsed.human_color = (Stone)human;
    parsed.difficulty = difficulty;
    parsed.move_count = move_count;

    for (int index = 0; index < move_count; ++index) {
        if (!split_line(&cursor, &line)
            || !parse_move_line(line, &parsed.moves[index])) {
            goto done;
        }
    }

    if (!split_line(&cursor, &line)
        || !parse_board_line(line, &parsed.expected.board)) {
        goto done;
    }
    int player = 0;
    int result = 0;
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "player", &player)
        || (player != (int)STONE_BLACK && player != (int)STONE_WHITE)) {
        goto done;
    }
    if (!split_line(&cursor, &line)
        || !parse_int_value(line, "result", &result)
        || result < (int)GAME_RESULT_ONGOING
        || result > (int)GAME_RESULT_DRAW) {
        goto done;
    }
    parsed.expected.current_player = (Stone)player;
    parsed.expected.result = (GameResult)result;

    /* Only an optional trailing newline may remain. */
    while (cursor != NULL && *cursor != '\0') {
        if (!split_line(&cursor, &line) || line[0] != '\0') {
            goto done;
        }
    }

    GameState replay;
    game_init(&replay);
    for (int index = 0; index < move_count; ++index) {
        if (replay.result != GAME_RESULT_ONGOING
            || !game_apply_move(&replay, parsed.moves[index])) {
            status = SAVE_STATUS_STATE;
            goto done;
        }
    }
    if (!game_is_consistent(&replay)
        || !board_matches(&replay.board, &parsed.expected.board)
        || replay.current_player != parsed.expected.current_player
        || replay.result != parsed.expected.result) {
        status = SAVE_STATUS_STATE;
        goto done;
    }

    parsed.expected = replay;
    *out = parsed;
    status = SAVE_STATUS_OK;

done:
    free(file);
    return status;
}
