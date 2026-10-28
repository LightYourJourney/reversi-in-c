#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_session.h"

#include "core/rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <time.h>
#endif

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#elif defined(__EMSCRIPTEN__)
    return (uint64_t)emscripten_get_now();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static bool send_hello(NetSession *session)
{
    const NetMessage hello = {.type = NET_MSG_HELLO,
                              .version = REVERSI_NET_VERSION};
    return net_client_send(session->client, &hello);
}

static void on_status(void *context, NetClientStatus status,
                      const char *reason)
{
    NetSession *session = context;
    (void)reason;
    switch (status) {
    case NET_CLIENT_CONNECTED:
        /* Protocol v1: hello is always the first message, including on a
         * reconnect before the rejoin request. */
        send_hello(session);
        if (session->rejoin_pending && session->token[0] != '\0') {
            NetMessage rejoin = {.type = NET_MSG_REJOIN};
            snprintf(rejoin.token, sizeof(rejoin.token), "%s",
                     session->token);
            net_client_send(session->client, &rejoin);
            session->phase = NET_SESSION_CONNECTING;
        } else {
            session->phase = NET_SESSION_LOBBY;
        }
        break;
    case NET_CLIENT_CONNECTING:
        session->phase = NET_SESSION_CONNECTING;
        break;
    case NET_CLIENT_DISCONNECTED:
        /* A finished game stays on screen even if the socket drops later. */
        if (session->phase != NET_SESSION_FINISHED) {
            session->phase = session->active ? NET_SESSION_DISCONNECTED
                                             : NET_SESSION_LOBBY;
        }
        session->reconnect_timer = 0.0f;
        break;
    case NET_CLIENT_FAILED:
        if (session->phase == NET_SESSION_FINISHED) {
            break;
        }
        if (session->active && session->token[0] != '\0'
            && session->reconnect_attempts < 5) {
            session->phase = NET_SESSION_DISCONNECTED;
            session->reconnect_timer = 0.0f;
        } else {
            session->phase = NET_SESSION_FAILED;
        }
        break;
    case NET_CLIENT_IDLE:
    default:
        break;
    }
}

static void apply_state(NetSession *session, const NetMessage *message)
{
    GameState state;
    if (!net_protocol_state_from_board(message->board, message->turn,
                                       message->result, &state)) {
        return;
    }
    session->game = state;
    session->seq = message->seq;
    session->has_last = message->has_last;
    session->last = message->last;
    session->pass = message->pass;
    session->result = message->result;
    session->state_dirty = true;
    session->error[0] = '\0';
    if (message->type == NET_MSG_START) {
        session->started = true;
    }
    session->phase = message->result == GAME_RESULT_ONGOING
        ? NET_SESSION_PLAYING : NET_SESSION_FINISHED;
}

static void on_message(void *context, const NetMessage *message)
{
    NetSession *session = context;
    switch (message->type) {
    case NET_MSG_WELCOME:
        snprintf(session->token, sizeof(session->token), "%s",
                 message->token);
        session->player = message->player;
        session->active = true;
        session->rejoin_pending = false;
        session->reconnect_attempts = 0;
        break;
    case NET_MSG_ROOM:
        session->error[0] = '\0';
        snprintf(session->code, sizeof(session->code), "%s", message->code);
        session->color = message->color;
        session->player = message->player;
        session->is_host = message->host;
        session->ready = message->ready;
        if (strcmp(message->status, "playing") == 0) {
            session->phase = session->result == GAME_RESULT_ONGOING
                ? NET_SESSION_PLAYING : NET_SESSION_FINISHED;
        } else if (strcmp(message->status, "finished") == 0) {
            session->phase = NET_SESSION_FINISHED;
        } else {
            session->phase = NET_SESSION_WAITING;
        }
        break;
    case NET_MSG_START:
    case NET_MSG_STATE:
        apply_state(session, message);
        break;
    case NET_MSG_GAMEOVER:
        session->result = message->result;
        session->game.result = message->result;
        session->state_dirty = true;
        snprintf(session->reason, sizeof(session->reason), "%s",
                 message->status);
        session->phase = NET_SESSION_FINISHED;
        session->active = true;
        break;
    case NET_MSG_OPPONENT:
        snprintf(session->opponent_status,
                 sizeof(session->opponent_status), "%s", message->status);
        break;
    case NET_MSG_ERROR:
        snprintf(session->error, sizeof(session->error), "%s",
                 message->error);
        if (strcmp(message->error, "rejoin_failed") == 0) {
            /* The room is gone (restart/expiry); the lobby must not keep
             * showing its code or the old opponent state. */
            session->active = false;
            session->token[0] = '\0';
            session->code[0] = '\0';
            session->opponent_status[0] = '\0';
            session->phase = NET_SESSION_LOBBY;
        }
        break;
    case NET_MSG_ROOMCLOSED:
        /* Keep a finished game (and its end reason) on screen when the room
         * disappears; only an unfinished room falls back to the lobby. */
        if (session->phase != NET_SESSION_FINISHED) {
            snprintf(session->reason, sizeof(session->reason), "%s",
                     message->status);
            session->phase = NET_SESSION_LOBBY;
        }
        session->active = false;
        session->code[0] = '\0';
        session->token[0] = '\0';
        break;
    case NET_MSG_PONG:
        session->latency_ms = (int)(now_ms() - session->ping_sent_ms);
        break;
    default:
        break;
    }
}

NetSession *net_session_create(void)
{
    NetSession *session = calloc(1, sizeof(*session));
    if (session == NULL) {
        return NULL;
    }
    session->phase = NET_SESSION_IDLE;
    session->result = GAME_RESULT_ONGOING;
    session->client = net_client_create(on_message, on_status, session);
    if (session->client == NULL) {
        free(session);
        return NULL;
    }
    return session;
}

void net_session_destroy(NetSession *session)
{
    if (session == NULL) {
        return;
    }
    net_client_destroy(session->client);
    free(session);
}

bool net_session_connect(NetSession *session, const char *host, int port)
{
    if (session == NULL || host == NULL || host[0] == '\0') {
        return false;
    }
    snprintf(session->host, sizeof(session->host), "%s", host);
    session->port = port;
    session->active = false;
    session->token[0] = '\0';
    session->rejoin_pending = false;
    session->reconnect_attempts = 0;
    return net_client_connect(session->client, host, port);
}

void net_session_disconnect(NetSession *session)
{
    if (session == NULL) {
        return;
    }
    net_client_disconnect(session->client);
    session->phase = NET_SESSION_IDLE;
    session->active = false;
}

void net_session_update(NetSession *session, float dt)
{
    if (session == NULL) {
        return;
    }
    net_client_poll(session->client);
    if (session->phase == NET_SESSION_DISCONNECTED
        && session->token[0] != '\0' && session->reconnect_attempts < 5) {
        session->reconnect_timer += dt;
        if (session->reconnect_timer >= 2.0f) {
            session->reconnect_timer = 0.0f;
            ++session->reconnect_attempts;
            session->rejoin_pending = true;
            net_client_connect(session->client, session->host, session->port);
        }
    }
    if (net_client_status(session->client) == NET_CLIENT_CONNECTED) {
        session->ping_timer += dt;
        if (session->ping_timer >= 2.0f) {
            session->ping_timer = 0.0f;
            const NetMessage ping = {.type = NET_MSG_PING};
            if (net_client_send(session->client, &ping)) {
                session->ping_sent_ms = now_ms();
            }
        }
    }
}

bool net_session_create_room(NetSession *session, Stone color)
{
    if (session == NULL || !stone_is_player(color)) {
        return false;
    }
    const NetMessage message = {.type = NET_MSG_CREATE, .color = color};
    return net_client_send(session->client, &message);
}

bool net_session_join_room(NetSession *session, const char *code,
                           Stone color)
{
    if (session == NULL || code == NULL || strlen(code) != REVERSI_NET_CODE_LEN
        || !stone_is_player(color)) {
        return false;
    }
    NetMessage message = {.type = NET_MSG_JOIN, .color = color};
    snprintf(message.code, sizeof(message.code), "%s", code);
    return net_client_send(session->client, &message);
}

bool net_session_ready(NetSession *session)
{
    if (session == NULL) {
        return false;
    }
    const NetMessage message = {.type = NET_MSG_READY};
    return net_client_send(session->client, &message);
}

bool net_session_can_move(const NetSession *session)
{
    return session != NULL && session->phase == NET_SESSION_PLAYING
        && session->game.result == GAME_RESULT_ONGOING
        && session->game.current_player == session->color;
}

bool net_session_move(NetSession *session, Move move)
{
    if (!net_session_can_move(session)
        || !rules_is_legal_move(&session->game.board, session->color, move)) {
        return false;
    }
    NetMessage message = {.type = NET_MSG_MOVE, .seq = session->seq,
                          .last = move, .has_last = true};
    return net_client_send(session->client, &message);
}

bool net_session_resign(NetSession *session)
{
    if (session == NULL || session->phase != NET_SESSION_PLAYING) {
        return false;
    }
    const NetMessage message = {.type = NET_MSG_RESIGN};
    return net_client_send(session->client, &message);
}

void net_session_leave(NetSession *session)
{
    if (session == NULL) {
        return;
    }
    if (session->active) {
        const NetMessage message = {.type = NET_MSG_LEAVE};
        net_client_send(session->client, &message);
    }
    session->active = false;
    session->code[0] = '\0';
    session->token[0] = '\0';
    session->phase = session->phase == NET_SESSION_FAILED
        ? NET_SESSION_FAILED : NET_SESSION_LOBBY;
}

const char *net_session_status_text(const NetSession *session)
{
    if (session == NULL) {
        return "Idle";
    }
    switch (session->phase) {
    case NET_SESSION_CONNECTING: return "Connecting...";
    case NET_SESSION_LOBBY: return "Lobby";
    case NET_SESSION_WAITING: return "Waiting for opponent";
    case NET_SESSION_PLAYING: return "Game in progress";
    case NET_SESSION_FINISHED: return "Game over";
    case NET_SESSION_DISCONNECTED: return "Reconnecting...";
    case NET_SESSION_FAILED: return "Connection failed";
    case NET_SESSION_IDLE:
    default:
        return "Offline";
    }
}
