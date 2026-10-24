#include "analysis/record.h"

#include "core/board.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_HEADER "reversi-record 1"
#define RECORD_MAX_TOKENS 80

static void set_error(char *error, size_t error_size, const char *text)
{
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", text);
    }
}

static bool token_is(const char *token, const char *expected)
{
    return strcmp(token, expected) == 0;
}

/* Splits a mutable line into whitespace-separated tokens. */
static int split_tokens(char *line, char **tokens, int capacity)
{
    int count = 0;
    char *cursor = line;
    while (*cursor != '\0') {
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (count >= capacity) {
            return -1;
        }
        tokens[count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }
    return count;
}

static bool parse_result(const char *token, GameResult *out_result,
                         int *out_diff)
{
    *out_diff = 0;
    if (strcmp(token, "-") == 0) {
        *out_result = GAME_RESULT_ONGOING;
        return true;
    }
    if (strcmp(token, "D") == 0) {
        *out_result = GAME_RESULT_DRAW;
        return true;
    }
    if ((token[0] != 'B' && token[0] != 'W') || token[1] != '+'
        || token[2] == '\0') {
        return false;
    }
    int diff = 0;
    for (const char *c = token + 2; *c != '\0'; ++c) {
        if (!isdigit((unsigned char)*c) || diff > 1000) {
            return false;
        }
        diff = diff * 10 + (*c - '0');
    }
    if (diff <= 0) {
        return false;
    }
    *out_result = token[0] == 'B' ? GAME_RESULT_BLACK_WIN
                                  : GAME_RESULT_WHITE_WIN;
    *out_diff = diff;
    return true;
}

static bool result_matches(const GameState *state, GameResult expected,
                           int diff)
{
    if (state->result != expected) {
        return false;
    }
    if (expected == GAME_RESULT_DRAW) {
        return diff == 0;
    }
    if (expected == GAME_RESULT_ONGOING) {
        return true;
    }
    const int black = board_count(&state->board, STONE_BLACK);
    const int white = board_count(&state->board, STONE_WHITE);
    const int actual = black > white ? black - white : white - black;
    return actual == diff;
}

static bool play_token(AnalysisSession *session, const char *token,
                       char *error, size_t error_size)
{
    if (token_is(token, "pass")) {
        if (!analysis_pass(session)) {
            set_error(error, error_size, "pass is not legal here");
            return false;
        }
        return true;
    }
    Move move;
    if (!move_from_notation(token, &move)) {
        set_error(error, error_size, "invalid move notation");
        return false;
    }
    if (!analysis_play_move(session, move)) {
        set_error(error, error_size, "illegal move in record");
        return false;
    }
    return true;
}

static bool play_tokens(AnalysisSession *session, char **tokens, int count,
                        char *error, size_t error_size)
{
    for (int i = 0; i < count; ++i) {
        if (!play_token(session, tokens[i], error, error_size)) {
            return false;
        }
    }
    return true;
}

static int main_node_at_ply(const AnalysisSession *session, int ply)
{
    int node = session->root;
    for (int i = 0; i < ply; ++i) {
        if (node < 0) {
            return -1;
        }
        node = session->positions[node].first_child;
    }
    return node;
}

static int main_line_length(const AnalysisSession *session)
{
    int node = session->root;
    int length = 0;
    while (session->positions[node].first_child >= 0) {
        node = session->positions[node].first_child;
        ++length;
    }
    return length;
}

bool analysis_record_read(AnalysisSession *session, const char *text,
                          char *error, size_t error_size)
{
    if (session == NULL || text == NULL) {
        set_error(error, error_size, "invalid arguments");
        return false;
    }
    if (strlen(text) >= ANALYSIS_MAX_RECORD_BYTES) {
        set_error(error, error_size, "record is too large");
        return false;
    }

    char work[ANALYSIS_MAX_RECORD_BYTES];
    memcpy(work, text, strlen(text) + 1);

    AnalysisSession *tmp = malloc(sizeof(*tmp));
    if (tmp == NULL) {
        set_error(error, error_size, "out of memory");
        return false;
    }
    analysis_init(tmp);

    bool have_header = false;
    bool have_moves = false;
    bool have_result = false;
    GameResult declared_result = GAME_RESULT_ONGOING;
    int declared_diff = 0;
    int variation_count = 0;
    int main_length = 0;
    bool ok = true;

    char *line = work;
    while (ok && *line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
            ++next;
        }
        const size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
        }

        char *after_line = next != NULL ? next : line + strlen(line);
        char *tokens[RECORD_MAX_TOKENS];
        const int count = split_tokens(line, tokens, RECORD_MAX_TOKENS);
        line = after_line;
        if (count < 0) {
            set_error(error, error_size, "too many tokens on a line");
            ok = false;
            break;
        }
        if (count == 0 || tokens[0][0] == '#') {
            continue;
        }

        if (!have_header) {
            if (count != 2 || !token_is(tokens[0], "reversi-record")
                || !token_is(tokens[1], "1")) {
                set_error(error, error_size, "missing reversi-record 1 header");
                ok = false;
                break;
            }
            have_header = true;
            continue;
        }

        if (token_is(tokens[0], "moves")) {
            if (have_moves) {
                set_error(error, error_size, "duplicate moves line");
                ok = false;
                break;
            }
            have_moves = true;
            if (!play_tokens(tmp, tokens + 1, count - 1, error, error_size)) {
                ok = false;
                break;
            }
            main_length = main_line_length(tmp);
            continue;
        }

        if (token_is(tokens[0], "result")) {
            if (have_result || count != 2
                || !parse_result(tokens[1], &declared_result,
                                 &declared_diff)) {
                set_error(error, error_size, "invalid result line");
                ok = false;
                break;
            }
            have_result = true;
            continue;
        }

        if (token_is(tokens[0], "variation")) {
            if (!have_moves) {
                set_error(error, error_size,
                          "variation before moves line");
                ok = false;
                break;
            }
            if (count < 3 || variation_count >= ANALYSIS_MAX_VARIATIONS) {
                set_error(error, error_size, "invalid or too many variations");
                ok = false;
                break;
            }
            char *end = NULL;
            const long ply = strtol(tokens[1], &end, 10);
            if (end == tokens[1] || *end != '\0' || ply < 0
                || ply > main_length) {
                set_error(error, error_size, "invalid variation ply");
                ok = false;
                break;
            }
            const int node = main_node_at_ply(tmp, (int)ply);
            if (node < 0 || !analysis_goto(tmp, node)) {
                set_error(error, error_size, "variation ply is not on the main line");
                ok = false;
                break;
            }
            if (!play_tokens(tmp, tokens + 2, count - 2, error, error_size)) {
                ok = false;
                break;
            }
            ++variation_count;
            continue;
        }

        set_error(error, error_size, "unknown record key");
        ok = false;
        break;
    }

    if (ok && !have_header) {
        set_error(error, error_size, "empty record");
        ok = false;
    }

    if (ok) {
        const int end = main_node_at_ply(tmp, main_length);
        if (end < 0 || !analysis_goto(tmp, end)) {
            ok = false;
        } else if (have_result
                   && !result_matches(&tmp->positions[end].state,
                                      declared_result, declared_diff)) {
            set_error(error, error_size,
                      "declared result does not match the moves");
            ok = false;
        }
    }

    if (ok) {
        *session = *tmp;
    }
    free(tmp);
    return ok;
}

typedef struct RecordWriter {
    char *buffer;
    size_t capacity;
    size_t used;
    bool failed;
} RecordWriter;

static void write_text(RecordWriter *writer, const char *text)
{
    if (writer->failed) {
        return;
    }
    const size_t length = strlen(text);
    if (writer->used + length + 1 > writer->capacity) {
        writer->failed = true;
        return;
    }
    memcpy(writer->buffer + writer->used, text, length);
    writer->used += length;
    writer->buffer[writer->used] = '\0';
}

static void write_via(RecordWriter *writer, const AnalysisPosition *node)
{
    if (node->via.is_pass) {
        write_text(writer, " pass");
        return;
    }
    char notation[3];
    if (!move_to_notation(node->via.move, notation)) {
        writer->failed = true;
        return;
    }
    write_text(writer, " ");
    write_text(writer, notation);
}

size_t analysis_record_write(const AnalysisSession *session, char *buffer,
                             size_t capacity)
{
    if (session == NULL || buffer == NULL || capacity == 0) {
        return 0;
    }
    buffer[0] = '\0';

    int main_length = 0;
    int node = session->root;
    while (session->positions[node].first_child >= 0) {
        node = session->positions[node].first_child;
        ++main_length;
    }
    if (main_length > ANALYSIS_MAX_PLIES) {
        return 0;
    }

    RecordWriter writer = {
        .buffer = buffer,
        .capacity = capacity,
        .used = 0,
        .failed = false
    };

    write_text(&writer, RECORD_HEADER "\n");

    const GameState *end = &session->positions[node].state;
    char result[32];
    if (end->result == GAME_RESULT_ONGOING) {
        snprintf(result, sizeof(result), "-");
    } else if (end->result == GAME_RESULT_DRAW) {
        snprintf(result, sizeof(result), "D");
    } else {
        const int black = board_count(&end->board, STONE_BLACK);
        const int white = board_count(&end->board, STONE_WHITE);
        snprintf(result, sizeof(result), "%c+%d",
                 end->result == GAME_RESULT_BLACK_WIN ? 'B' : 'W',
                 black > white ? black - white : white - black);
    }
    write_text(&writer, "result ");
    write_text(&writer, result);
    write_text(&writer, "\n");

    write_text(&writer, "moves");
    node = session->root;
    for (int i = 0; i < main_length; ++i) {
        node = session->positions[node].first_child;
        write_via(&writer, &session->positions[node]);
    }
    write_text(&writer, "\n");

    int variations = 0;
    node = session->root;
    for (int ply = 0; ply <= main_length; ++ply) {
        for (int child = session->positions[node].first_child; child >= 0;
             child = session->positions[child].next_sibling) {
            if (child == session->positions[node].first_child) {
                continue;
            }
            if (variations >= ANALYSIS_MAX_VARIATIONS) {
                buffer[0] = '\0';
                return 0;
            }
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "variation %d", ply);
            write_text(&writer, prefix);
            int line_node = child;
            while (line_node >= 0) {
                write_via(&writer, &session->positions[line_node]);
                line_node = session->positions[line_node].first_child;
            }
            write_text(&writer, "\n");
            ++variations;
        }
        if (ply < main_length) {
            node = session->positions[node].first_child;
        }
    }

    if (writer.failed) {
        buffer[0] = '\0';
        return 0;
    }
    return writer.used;
}
