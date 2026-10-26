#include "net/net_protocol.h"

#include "core/board.h"
#include "core/rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct NetFieldSet {
    bool version;
    bool code;
    bool token;
    bool color;
    bool turn;
    bool seq;
    bool result;
    bool pass;
    bool last;
    bool board;
    bool player;
    bool host;
    bool ready;
    bool status;
    bool error;
} NetFieldSet;

static const char *const TYPE_NAMES[] = {
    "hello", "create", "join", "ready", "move", "resign", "ping",
    "rejoin", "leave", "welcome", "room", "start", "state", "error",
    "opponent", "gameover", "roomclosed", "pong"
};

const char *net_message_type_name(NetMessageType type)
{
    if (type < NET_MSG_HELLO || type > NET_MSG_PONG) {
        return "unknown";
    }
    return TYPE_NAMES[type];
}

static bool parse_type(const char *name, NetMessageType *out)
{
    for (int i = 0; i <= (int)NET_MSG_PONG; ++i) {
        if (strcmp(name, TYPE_NAMES[i]) == 0) {
            *out = (NetMessageType)i;
            return true;
        }
    }
    return false;
}

static bool is_alnum_upper(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool is_lower_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static bool valid_code(const char *text)
{
    if (strlen(text) != REVERSI_NET_CODE_LEN) {
        return false;
    }
    for (const char *c = text; *c != '\0'; ++c) {
        if (!is_alnum_upper(*c)) {
            return false;
        }
    }
    return true;
}

static bool valid_token(const char *text)
{
    if (strlen(text) != REVERSI_NET_TOKEN_LEN) {
        return false;
    }
    for (const char *c = text; *c != '\0'; ++c) {
        if (!is_lower_hex(*c)) {
            return false;
        }
    }
    return true;
}

static bool valid_status(const char *text)
{
    const size_t length = strlen(text);
    if (length == 0 || length >= 24) {
        return false;
    }
    for (const char *c = text; *c != '\0'; ++c) {
        if (!(islower((unsigned char)*c) || isdigit((unsigned char)*c)
              || *c == '_' || *c == '-')) {
            return false;
        }
    }
    return true;
}

static bool parse_bool(const char *text, bool *out)
{
    if (strcmp(text, "0") == 0) {
        *out = false;
        return true;
    }
    if (strcmp(text, "1") == 0) {
        *out = true;
        return true;
    }
    return false;
}

static bool parse_color(const char *text, Stone *out)
{
    if (strcmp(text, "B") == 0) {
        *out = STONE_BLACK;
        return true;
    }
    if (strcmp(text, "W") == 0) {
        *out = STONE_WHITE;
        return true;
    }
    return false;
}

static bool parse_result(const char *text, GameResult *out)
{
    if (strcmp(text, "O") == 0) {
        *out = GAME_RESULT_ONGOING;
        return true;
    }
    if (strcmp(text, "B") == 0) {
        *out = GAME_RESULT_BLACK_WIN;
        return true;
    }
    if (strcmp(text, "W") == 0) {
        *out = GAME_RESULT_WHITE_WIN;
        return true;
    }
    if (strcmp(text, "D") == 0) {
        *out = GAME_RESULT_DRAW;
        return true;
    }
    return false;
}

static bool parse_seq(const char *text, int *out)
{
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > 1000000) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool parse_board(const char *text, char out[65])
{
    if (strlen(text) != REVERSI_BOARD_CELL_COUNT) {
        return false;
    }
    for (int i = 0; i < REVERSI_BOARD_CELL_COUNT; ++i) {
        if (text[i] < '0' || text[i] > '2') {
            return false;
        }
    }
    memcpy(out, text, REVERSI_BOARD_CELL_COUNT);
    out[REVERSI_BOARD_CELL_COUNT] = '\0';
    return true;
}

static void required_fields(NetMessageType type, NetFieldSet *set)
{
    memset(set, 0, sizeof(*set));
    switch (type) {
    case NET_MSG_HELLO: set->version = true; break;
    case NET_MSG_CREATE: set->color = true; break;
    case NET_MSG_JOIN: set->code = true; set->color = true; break;
    case NET_MSG_MOVE: set->last = true; set->seq = true; break;
    case NET_MSG_REJOIN: set->token = true; break;
    case NET_MSG_WELCOME:
        set->version = true; set->token = true; set->player = true;
        break;
    case NET_MSG_ROOM:
        set->code = true; set->color = true; set->player = true;
        set->host = true; set->ready = true; set->status = true;
        break;
    case NET_MSG_START:
        set->turn = true; set->seq = true; set->board = true;
        break;
    case NET_MSG_STATE:
        set->turn = true; set->seq = true; set->board = true;
        set->pass = true; set->result = true; set->last = true;
        break;
    case NET_MSG_ERROR: set->error = true; break;
    case NET_MSG_OPPONENT: set->status = true; break;
    case NET_MSG_GAMEOVER: set->result = true; set->status = true; break;
    case NET_MSG_ROOMCLOSED: set->status = true; break;
    case NET_MSG_READY:
    case NET_MSG_RESIGN:
    case NET_MSG_PING:
    case NET_MSG_LEAVE:
    case NET_MSG_PONG:
        break;
    }
}

static bool fields_match(const NetFieldSet *set, NetMessageType type)
{
    NetFieldSet required;
    required_fields(type, &required);
    return memcmp(set, &required, sizeof(required)) == 0;
}

bool net_protocol_decode(const char *text, NetDirection direction,
                         NetMessage *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    if (length == 0 || length > REVERSI_NET_MAX_MESSAGE) {
        return false;
    }
    if (memchr(text, '\r', length) != NULL
        || memchr(text, '\n', length) != NULL) {
        return false;
    }

    char work[REVERSI_NET_MAX_MESSAGE + 1];
    memcpy(work, text, length + 1);
    memset(out, 0, sizeof(*out));
    out->seq = -1;
    out->player = 0;

    NetFieldSet seen;
    memset(&seen, 0, sizeof(seen));

    char *cursor = work;
    char *token = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        ++cursor;
    }
    if (*cursor == ' ') {
        *cursor++ = '\0';
    }
    if (!parse_type(token, &out->type)) {
        return false;
    }

    while (*cursor != '\0') {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        token = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
        char *equals = strchr(token, '=');
        if (equals == NULL) {
            return false;
        }
        *equals = '\0';
        const char *key = token;
        const char *value = equals + 1;

        if (strcmp(key, "v") == 0) {
            int version = 0;
            if (seen.version || sscanf(value, "%d", &version) != 1
                || version != REVERSI_NET_VERSION) {
                return false;
            }
            out->version = version;
            seen.version = true;
        } else if (strcmp(key, "code") == 0) {
            if (seen.code || !valid_code(value)) {
                return false;
            }
            snprintf(out->code, sizeof(out->code), "%s", value);
            seen.code = true;
        } else if (strcmp(key, "token") == 0) {
            if (seen.token || !valid_token(value)) {
                return false;
            }
            snprintf(out->token, sizeof(out->token), "%s", value);
            seen.token = true;
        } else if (strcmp(key, "color") == 0) {
            if (seen.color || !parse_color(value, &out->color)) {
                return false;
            }
            seen.color = true;
        } else if (strcmp(key, "turn") == 0) {
            if (seen.turn || !parse_color(value, &out->turn)) {
                return false;
            }
            seen.turn = true;
        } else if (strcmp(key, "seq") == 0) {
            if (seen.seq || !parse_seq(value, &out->seq)) {
                return false;
            }
            seen.seq = true;
        } else if (strcmp(key, "result") == 0) {
            if (seen.result || !parse_result(value, &out->result)) {
                return false;
            }
            seen.result = true;
        } else if (strcmp(key, "pass") == 0) {
            if (seen.pass || !parse_bool(value, &out->pass)) {
                return false;
            }
            seen.pass = true;
        } else if (strcmp(key, "cell") == 0) {
            if (seen.last || !move_from_notation(value, &out->last)) {
                return false;
            }
            out->has_last = true;
            seen.last = true;
        } else if (strcmp(key, "last") == 0) {
            if (seen.last) {
                return false;
            }
            if (strcmp(value, "-") == 0) {
                out->has_last = false;
            } else if (!move_from_notation(value, &out->last)) {
                return false;
            } else {
                out->has_last = true;
            }
            seen.last = true;
        } else if (strcmp(key, "board") == 0) {
            if (seen.board || !parse_board(value, out->board)) {
                return false;
            }
            seen.board = true;
        } else if (strcmp(key, "player") == 0) {
            if (seen.player || (strcmp(value, "1") != 0
                                && strcmp(value, "2") != 0)) {
                return false;
            }
            out->player = value[0] - '0';
            seen.player = true;
        } else if (strcmp(key, "host") == 0) {
            if (seen.host || !parse_bool(value, &out->host)) {
                return false;
            }
            seen.host = true;
        } else if (strcmp(key, "ready") == 0) {
            if (seen.ready || !parse_bool(value, &out->ready)) {
                return false;
            }
            seen.ready = true;
        } else if (strcmp(key, "status") == 0) {
            if (seen.status || !valid_status(value)) {
                return false;
            }
            snprintf(out->status, sizeof(out->status), "%s", value);
            seen.status = true;
        } else if (strcmp(key, "error") == 0) {
            if (seen.error || !valid_status(value)) {
                return false;
            }
            snprintf(out->error, sizeof(out->error), "%s", value);
            seen.error = true;
        } else {
            return false;
        }
    }

    if (!fields_match(&seen, out->type)) {
        return false;
    }
    const bool server_only = out->type >= NET_MSG_WELCOME;
    if ((direction == NET_DIRECTION_CLIENT_TO_SERVER) != !server_only) {
        return false;
    }
    return true;
}

static bool encode_field(char *buffer, size_t capacity, size_t *used,
                         const char *key, const char *value)
{
    const int written = snprintf(buffer + *used, capacity - *used, " %s=%s",
                                 key, value);
    if (written < 0 || (size_t)written >= capacity - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

size_t net_protocol_encode(const NetMessage *message, char *buffer,
                           size_t capacity)
{
    if (message == NULL || buffer == NULL || capacity == 0) {
        return 0;
    }
    NetFieldSet fields;
    required_fields(message->type, &fields);
    buffer[0] = '\0';
    size_t used = 0;
    const int written = snprintf(buffer, capacity, "%s",
                                 net_message_type_name(message->type));
    if (written < 0 || (size_t)written >= capacity) {
        return 0;
    }
    used = (size_t)written;

    char number[32];
    if (fields.version) {
        snprintf(number, sizeof(number), "%d", message->version);
        if (!encode_field(buffer, capacity, &used, "v", number)) {
            return 0;
        }
    }
    if (fields.code
        && !encode_field(buffer, capacity, &used, "code", message->code)) {
        return 0;
    }
    if (fields.token
        && !encode_field(buffer, capacity, &used, "token", message->token)) {
        return 0;
    }
    if (fields.color
        && !encode_field(buffer, capacity, &used, "color",
                         message->color == STONE_BLACK ? "B" : "W")) {
        return 0;
    }
    if (fields.turn
        && !encode_field(buffer, capacity, &used, "turn",
                         message->turn == STONE_BLACK ? "B" : "W")) {
        return 0;
    }
    if (fields.seq) {
        snprintf(number, sizeof(number), "%d", message->seq);
        if (!encode_field(buffer, capacity, &used, "seq", number)) {
            return 0;
        }
    }
    if (fields.result) {
        const char *result = message->result == GAME_RESULT_BLACK_WIN ? "B"
            : message->result == GAME_RESULT_WHITE_WIN ? "W"
            : message->result == GAME_RESULT_DRAW ? "D" : "O";
        if (!encode_field(buffer, capacity, &used, "result", result)) {
            return 0;
        }
    }
    if (fields.pass
        && !encode_field(buffer, capacity, &used, "pass",
                         message->pass ? "1" : "0")) {
        return 0;
    }
    if (fields.last) {
        char notation[3];
        const char *value = "-";
        if (message->has_last && move_to_notation(message->last, notation)) {
            value = notation;
        }
        const char *key = message->type == NET_MSG_MOVE ? "cell" : "last";
        if (!encode_field(buffer, capacity, &used, key, value)) {
            return 0;
        }
    }
    if (fields.board
        && !encode_field(buffer, capacity, &used, "board", message->board)) {
        return 0;
    }
    if (fields.player) {
        snprintf(number, sizeof(number), "%d", message->player);
        if (!encode_field(buffer, capacity, &used, "player", number)) {
            return 0;
        }
    }
    if (fields.host
        && !encode_field(buffer, capacity, &used, "host",
                         message->host ? "1" : "0")) {
        return 0;
    }
    if (fields.ready
        && !encode_field(buffer, capacity, &used, "ready",
                         message->ready ? "1" : "0")) {
        return 0;
    }
    if (fields.status
        && !encode_field(buffer, capacity, &used, "status", message->status)) {
        return 0;
    }
    if (fields.error
        && !encode_field(buffer, capacity, &used, "error", message->error)) {
        return 0;
    }
    if (used > REVERSI_NET_MAX_MESSAGE) {
        buffer[0] = '\0';
        return 0;
    }
    NetDirection direction = message->type >= NET_MSG_WELCOME
        ? NET_DIRECTION_SERVER_TO_CLIENT : NET_DIRECTION_CLIENT_TO_SERVER;
    NetMessage check;
    if (!net_protocol_decode(buffer, direction, &check)) {
        buffer[0] = '\0';
        return 0;
    }
    return used;
}

bool net_protocol_board_from_state(const GameState *state, char out[65])
{
    if (state == NULL || !board_is_valid(&state->board)) {
        return false;
    }
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        const Stone stone = state->board.cells[index / REVERSI_BOARD_SIZE]
                                            [index % REVERSI_BOARD_SIZE];
        out[index] = (char)('0' + (int)stone);
    }
    out[REVERSI_BOARD_CELL_COUNT] = '\0';
    return true;
}

bool net_protocol_state_from_board(const char *board, Stone turn,
                                   GameResult result, GameState *out)
{
    if (board == NULL || out == NULL
        || strlen(board) != REVERSI_BOARD_CELL_COUNT
        || !stone_is_player(turn)) {
        return false;
    }
    GameState state;
    memset(&state, 0, sizeof(state));
    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        const char cell = board[index];
        if (cell < '0' || cell > '2') {
            return false;
        }
        state.board.cells[index / REVERSI_BOARD_SIZE]
                         [index % REVERSI_BOARD_SIZE] = (Stone)(cell - '0');
    }
    state.current_player = turn;
    state.result = result;
    if (!game_is_consistent(&state)) {
        return false;
    }
    *out = state;
    return true;
}
