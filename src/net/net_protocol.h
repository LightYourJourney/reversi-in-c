#ifndef REVERSI_NET_NET_PROTOCOL_H
#define REVERSI_NET_NET_PROTOCOL_H

#include "core/game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REVERSI_NET_VERSION 1
#define REVERSI_NET_MAX_MESSAGE 2048
#define REVERSI_NET_CODE_LEN 6
#define REVERSI_NET_TOKEN_LEN 32

typedef enum NetDirection {
    NET_DIRECTION_CLIENT_TO_SERVER = 0,
    NET_DIRECTION_SERVER_TO_CLIENT
} NetDirection;

typedef enum NetMessageType {
    NET_MSG_HELLO = 0,
    NET_MSG_CREATE,
    NET_MSG_JOIN,
    NET_MSG_READY,
    NET_MSG_MOVE,
    NET_MSG_RESIGN,
    NET_MSG_PING,
    NET_MSG_REJOIN,
    NET_MSG_LEAVE,
    NET_MSG_WELCOME,
    NET_MSG_ROOM,
    NET_MSG_START,
    NET_MSG_STATE,
    NET_MSG_ERROR,
    NET_MSG_OPPONENT,
    NET_MSG_GAMEOVER,
    NET_MSG_ROOMCLOSED,
    NET_MSG_PONG
} NetMessageType;

typedef struct NetMessage {
    NetMessageType type;
    int version;
    char code[REVERSI_NET_CODE_LEN + 1];
    char token[REVERSI_NET_TOKEN_LEN + 1];
    Stone color;
    Stone turn;
    int seq;
    GameResult result;
    bool pass;
    bool has_last;
    Move last;
    char board[REVERSI_BOARD_CELL_COUNT + 1];
    int player;
    bool host;
    bool ready;
    char status[24];
    char error[32];
} NetMessage;

/*
 * Parses one protocol line. Unknown keys, bad field values, missing required
 * fields and oversized messages are rejected; nothing is guessed.
 */
bool net_protocol_decode(const char *text, NetDirection direction,
                         NetMessage *out);

/*
 * Serializes a message in canonical field order. Returns the length or 0
 * when the message is invalid or the buffer is too small.
 */
size_t net_protocol_encode(const NetMessage *message, char *buffer,
                           size_t capacity);

const char *net_message_type_name(NetMessageType type);

/* Canonical board string: 64 characters of '0' (empty), '1' (black), '2'
 * (white). */
bool net_protocol_board_from_state(const GameState *state, char out[65]);
bool net_protocol_state_from_board(const char *board, Stone turn,
                                   GameResult result, GameState *out);

#endif
