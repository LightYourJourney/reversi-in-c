#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_server.h"

#include "core/game.h"
#include "core/rules.h"
#include "net/net_protocol.h"
#include "net/net_ws.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <signal.h>
#endif
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
typedef SOCKET NetSocket;
typedef int NetSockLen;
#define NET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetSocket;
typedef socklen_t NetSockLen;
#define NET_INVALID (-1)
#endif

#define REVERSI_NET_SERVER_MAX_CONNECTIONS 48
#define REVERSI_NET_SERVER_MAX_ROOMS 32
#define REVERSI_NET_SERVER_READ_CAP 8192
#define REVERSI_NET_SERVER_SEND_CAP 16384
#define REVERSI_NET_SERVER_TOKEN_CAP 40
#define REVERSI_NET_SERVER_TOKEN_RATE 20
#define REVERSI_NET_SERVER_HANDSHAKE_MS 5000
#define REVERSI_NET_SERVER_IDLE_MS 120000
#define REVERSI_NET_SERVER_ROOM_WAIT_MS 600000
#define REVERSI_NET_SERVER_ROOM_DONE_MS 120000

typedef enum ConnectionState {
    CONNECTION_FREE = 0,
    CONNECTION_HANDSHAKE,
    CONNECTION_OPEN,
    CONNECTION_CLOSING
} ConnectionState;

typedef struct ServerConnection {
    ConnectionState state;
    NetSocket socket;
    uint8_t input[REVERSI_NET_SERVER_READ_CAP];
    size_t input_length;
    uint8_t output[REVERSI_NET_SERVER_SEND_CAP];
    size_t output_length;
    NetWsParser parser;
    uint64_t handshake_deadline;
    uint64_t last_active;
    int tokens;
    uint64_t tokens_ms;
    int room;
    int seat;
} ServerConnection;

typedef struct ServerPlayer {
    bool seated;
    char token[REVERSI_NET_TOKEN_LEN + 1];
    Stone color;
    bool ready;
    int connection;
    uint64_t rejoin_deadline;
} ServerPlayer;

typedef struct ServerRoom {
    bool used;
    char code[REVERSI_NET_CODE_LEN + 1];
    bool playing;
    bool finished;
    ServerPlayer players[2];
    GameState game;
    int seq;
    Move last;
    bool has_last;
    bool pass;
    GameResult result;
    uint64_t move_deadline;
    uint64_t expires;
} ServerRoom;

struct NetServer {
    NetServerConfig config;
    NetSocket listener;
    int port;
    bool started;
    ServerConnection connections[REVERSI_NET_SERVER_MAX_CONNECTIONS];
    ServerRoom rooms[REVERSI_NET_SERVER_MAX_ROOMS];
    uint64_t rng;
};

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static bool os_random(uint8_t *buffer, size_t length)
{
#if defined(_WIN32)
    return BCryptGenRandom(NULL, buffer, (ULONG)length,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE *file = fopen("/dev/urandom", "rb");
    if (file == NULL) {
        return false;
    }
    const size_t read = fread(buffer, 1, length, file);
    fclose(file);
    return read == length;
#endif
}

static uint64_t next_rng(NetServer *server)
{
    uint64_t x = server->rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    server->rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void log_line(NetServer *server, const char *format, ...)
{
    if (server->config.quiet) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static bool socket_nonblocking(NetSocket socket)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void socket_close(NetSocket socket)
{
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

/* Returns bytes read, 0 on orderly close, -1 when no data is available. */
static int socket_read(NetSocket socket, uint8_t *buffer, size_t capacity)
{
    const int result = (int)recv(socket, (char *)buffer, (int)capacity, 0);
    if (result > 0) {
        return result;
    }
    if (result == 0) {
        return 0;
    }
#if defined(_WIN32)
    const int error = WSAGetLastError();
    return (error == WSAEWOULDBLOCK) ? -1 : 0;
#else
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? -1 : 0;
#endif
}

/* Returns bytes written or -1 when no progress is possible. */
static int socket_write(NetSocket socket, const uint8_t *data, size_t length)
{
    const int result = (int)send(socket, (const char *)data, (int)length, 0);
    if (result > 0) {
        return result;
    }
#if defined(_WIN32)
    const int error = WSAGetLastError();
    return (error == WSAEWOULDBLOCK) ? -1 : -2;
#else
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK
                       || errno == EINTR)) {
        return -1;
    }
    return -2;
#endif
}

static void detach_player(NetServer *server, int room_index, int seat,
                          const char *status, bool forfeit);

static void connection_schedule_close(NetServer *server, int index)
{
    ServerConnection *connection = &server->connections[index];
    if (connection->state == CONNECTION_FREE) {
        return;
    }
    if (connection->room >= 0) {
        ServerRoom *room = &server->rooms[connection->room];
        if (room->used && connection->seat >= 0
            && room->players[connection->seat].connection == index) {
            detach_player(server, connection->room, connection->seat,
                          "disconnected", false);
        }
        connection->room = -1;
        connection->seat = -1;
    }

    if (connection->socket != NET_INVALID) {
        socket_close(connection->socket);
        connection->socket = NET_INVALID;
    }
    connection->state = CONNECTION_FREE;
    connection->input_length = 0;
    connection->output_length = 0;
    connection->room = -1;
    connection->seat = -1;
}

static bool connection_queue(NetServer *server, int index,
                             const uint8_t *data, size_t length)
{
    ServerConnection *connection = &server->connections[index];
    if (connection->output_length + length > REVERSI_NET_SERVER_SEND_CAP) {
        return false;
    }
    memcpy(connection->output + connection->output_length, data, length);
    connection->output_length += length;
    return true;
}

static bool connection_send_text(NetServer *server, int index,
                                 const char *text)
{
    if (index < 0 || index >= REVERSI_NET_SERVER_MAX_CONNECTIONS) {
        return false;
    }
    uint8_t frame[REVERSI_NET_MAX_MESSAGE + 16];
    const size_t length = net_ws_encode(
        0x1, (const uint8_t *)text, strlen(text), false, NULL, frame,
        sizeof(frame));
    return length > 0 && connection_queue(server, index, frame, length);
}

static void connection_flush(NetServer *server, int index)
{
    ServerConnection *connection = &server->connections[index];
    while (connection->state != CONNECTION_FREE
           && connection->output_length > 0) {
        const int written = socket_write(
            connection->socket, connection->output, connection->output_length);
        if (written == -1) {
            return;
        }
        if (written <= 0) {
            connection_schedule_close(server, index);
            return;
        }
        memmove(connection->output, connection->output + written,
                connection->output_length - (size_t)written);
        connection->output_length -= (size_t)written;
    }
}

static void send_message(NetServer *server, int connection,
                         const NetMessage *message)
{
    if (connection < 0 || connection >= REVERSI_NET_SERVER_MAX_CONNECTIONS) {
        return;
    }
    char text[REVERSI_NET_MAX_MESSAGE + 1];
    if (net_protocol_encode(message, text, sizeof(text)) == 0) {
        return;
    }
    if (!connection_send_text(server, connection, text)) {
        connection_schedule_close(server, connection);
    }
}

static void send_error(NetServer *server, int connection, const char *code)
{
    NetMessage message = {.type = NET_MSG_ERROR};
    snprintf(message.error, sizeof(message.error), "%s", code);
    send_message(server, connection, &message);
}

static int find_room(NetServer *server, const char *code)
{
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_ROOMS; ++i) {
        if (server->rooms[i].used
            && strcmp(server->rooms[i].code, code) == 0) {
            return i;
        }
    }
    return -1;
}

static bool generate_code(NetServer *server, char out[7])
{
    static const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int attempt = 0; attempt < 100; ++attempt) {
        uint8_t bytes[REVERSI_NET_CODE_LEN];
        if (!os_random(bytes, sizeof(bytes))) {
            for (size_t i = 0; i < sizeof(bytes); ++i) {
                bytes[i] = (uint8_t)next_rng(server);
            }
        }
        for (int i = 0; i < REVERSI_NET_CODE_LEN; ++i) {
            out[i] = ALPHABET[bytes[i] % (sizeof(ALPHABET) - 1)];
        }
        out[REVERSI_NET_CODE_LEN] = '\0';
        if (find_room(server, out) < 0) {
            return true;
        }
    }
    return false;
}

static bool generate_token(NetServer *server, char out[33])
{
    uint8_t bytes[16];
    if (!os_random(bytes, sizeof(bytes))) {
        for (size_t i = 0; i < sizeof(bytes); ++i) {
            bytes[i] = (uint8_t)next_rng(server);
        }
    }
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = HEX[bytes[i] >> 4];
        out[i * 2 + 1] = HEX[bytes[i] & 0x0F];
    }
    out[32] = '\0';
    return true;
}

static void room_reset(ServerRoom *room)
{
    memset(room, 0, sizeof(*room));
    room->players[0].connection = -1;
    room->players[1].connection = -1;
}

static void room_broadcast(NetServer *server, ServerRoom *room,
                           const NetMessage *message)
{
    send_message(server, room->players[0].connection, message);
    send_message(server, room->players[1].connection, message);
}

static void room_close(NetServer *server, int room_index, const char *status)
{
    ServerRoom *room = &server->rooms[room_index];
    if (!room->used) {
        return;
    }
    NetMessage closed = {.type = NET_MSG_ROOMCLOSED};
    snprintf(closed.status, sizeof(closed.status), "%s", status);
    room_broadcast(server, room, &closed);
    for (int seat = 0; seat < 2; ++seat) {
        const int index = room->players[seat].connection;
        if (index >= 0) {
            server->connections[index].room = -1;
            server->connections[index].seat = -1;
        }
    }
    room_reset(room);
}

static void room_send_room(NetServer *server, ServerRoom *room, int seat)
{
    const int index = room->players[seat].connection;
    if (index < 0) {
        return;
    }
    NetMessage message = {
        .type = NET_MSG_ROOM,
        .color = room->players[seat].color,
        .player = seat + 1,
        .host = seat == 0,
        .ready = room->players[seat].ready
    };
    snprintf(message.code, sizeof(message.code), "%s", room->code);
    snprintf(message.status, sizeof(message.status), "%s",
             room->playing ? "playing"
                 : room->finished ? "finished" : "waiting");
    send_message(server, index, &message);
}

static void room_send_state(NetServer *server, ServerRoom *room, bool initial)
{
    NetMessage message = {
        .type = initial ? NET_MSG_START : NET_MSG_STATE,
        .turn = room->game.current_player,
        .seq = room->seq,
        .result = room->game.result,
        .pass = room->pass,
        .has_last = room->has_last,
        .last = room->last
    };
    net_protocol_board_from_state(&room->game, message.board);
    room_broadcast(server, room, &message);
}

static void room_finish(NetServer *server, int room_index, GameResult result,
                        const char *status)
{
    ServerRoom *room = &server->rooms[room_index];
    room->playing = false;
    room->finished = true;
    room->result = result;
    room->game.result = result;
    room->expires = now_ms() + REVERSI_NET_SERVER_ROOM_DONE_MS;
    room_send_state(server, room, false);
    NetMessage over = {.type = NET_MSG_GAMEOVER, .result = result};
    snprintf(over.status, sizeof(over.status), "%s", status);
    room_broadcast(server, room, &over);
    log_line(server, "room %s finished (%s)", room->code, status);
}

static int room_seat_for_connection(ServerRoom *room, int connection)
{
    for (int seat = 0; seat < 2; ++seat) {
        if (room->players[seat].seated
            && room->players[seat].connection == connection) {
            return seat;
        }
    }
    return -1;
}

static void start_game(NetServer *server, int room_index)
{
    ServerRoom *room = &server->rooms[room_index];
    game_init(&room->game);
    room->seq = 0;
    room->has_last = false;
    room->pass = false;
    room->result = GAME_RESULT_ONGOING;
    room->playing = true;
    room->finished = false;
    room->move_deadline = now_ms() + (uint64_t)server->config.move_timeout_ms;
    room_send_state(server, room, true);
    room_send_room(server, room, 0);
    room_send_room(server, room, 1);
    log_line(server, "room %s started", room->code);
}

static void detach_player(NetServer *server, int room_index, int seat,
                          const char *status, bool forfeit)
{
    ServerRoom *room = &server->rooms[room_index];
    ServerPlayer *player = &room->players[seat];
    const int other = 1 - seat;
    const bool playing = room->playing;
    player->connection = -1;
    player->ready = false;
    if (playing) {
        if (forfeit) {
            const Stone winner = room->players[other].color;
            const GameResult result = winner == STONE_BLACK
                ? GAME_RESULT_BLACK_WIN : GAME_RESULT_WHITE_WIN;
            room_finish(server, room_index, result, status);
        } else {
            player->rejoin_deadline = now_ms()
                + (uint64_t)server->config.reconnect_timeout_ms;
            NetMessage notice = {.type = NET_MSG_OPPONENT};
            snprintf(notice.status, sizeof(notice.status), "disconnected");
            send_message(server, room->players[other].connection, &notice);
            log_line(server, "room %s player %d disconnected", room->code,
                     seat + 1);
        }
        return;
    }
    room_close(server, room_index, status);
}

static void handle_move(NetServer *server, int connection, NetMessage *message)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room < 0) {
        send_error(server, connection, "not_in_room");
        return;
    }
    ServerRoom *room = &server->rooms[conn->room];
    if (!room->playing) {
        send_error(server, connection, "not_playing");
        return;
    }
    const int seat = room_seat_for_connection(room, connection);
    if (seat < 0 || room->players[seat].color != room->game.current_player) {
        send_error(server, connection, "not_your_turn");
        return;
    }
    if (message->seq != room->seq) {
        send_error(server, connection, "stale_state");
        return;
    }
    if (!message->has_last
        || !rules_is_legal_move(&room->game.board,
                                room->game.current_player, message->last)
        || room->game.result != GAME_RESULT_ONGOING) {
        send_error(server, connection, "illegal_move");
        return;
    }
    const Move move = message->last;
    const Stone mover = room->game.current_player;
    if (!game_apply_move(&room->game, move)) {
        send_error(server, connection, "illegal_move");
        return;
    }
    ++room->seq;
    room->last = move;
    room->has_last = true;
    room->pass = room->game.result == GAME_RESULT_ONGOING
        && room->game.current_player == mover;
    room->move_deadline = now_ms() + (uint64_t)server->config.move_timeout_ms;
    if (room->game.result != GAME_RESULT_ONGOING) {
        room_finish(server, conn->room, room->game.result, "normal");
    } else {
        room_send_state(server, room, false);
    }
}

static void send_room_snapshot(NetServer *server, ServerRoom *room, int seat)
{
    room_send_room(server, room, seat);
    if (room->playing || room->finished) {
        NetMessage state = {
            .type = NET_MSG_STATE,
            .turn = room->game.current_player,
            .seq = room->seq,
            .result = room->game.result,
            .pass = room->pass,
            .has_last = room->has_last,
            .last = room->last
        };
        net_protocol_board_from_state(&room->game, state.board);
        send_message(server, room->players[seat].connection, &state);
        if (room->finished) {
            NetMessage over = {.type = NET_MSG_GAMEOVER,
                               .result = room->result};
            snprintf(over.status, sizeof(over.status), "normal");
            send_message(server, room->players[seat].connection, &over);
        }
    }
}

static void handle_rejoin(NetServer *server, int connection,
                          NetMessage *message)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room >= 0) {
        send_error(server, connection, "already_in_room");
        return;
    }
    for (int r = 0; r < REVERSI_NET_SERVER_MAX_ROOMS; ++r) {
        ServerRoom *room = &server->rooms[r];
        if (!room->used) {
            continue;
        }
        for (int seat = 0; seat < 2; ++seat) {
            ServerPlayer *player = &room->players[seat];
            if (player->seated && player->connection < 0
                && strcmp(player->token, message->token) == 0
                && now_ms() <= player->rejoin_deadline) {
                player->connection = connection;
                player->rejoin_deadline = 0;
                conn->room = r;
                conn->seat = seat;
                generate_token(server, player->token);
                NetMessage welcome = {
                    .type = NET_MSG_WELCOME,
                    .version = REVERSI_NET_VERSION,
                    .player = seat + 1
                };
                snprintf(welcome.token, sizeof(welcome.token), "%s",
                         player->token);
                send_message(server, connection, &welcome);
                send_room_snapshot(server, room, seat);
                NetMessage notice = {.type = NET_MSG_OPPONENT};
                snprintf(notice.status, sizeof(notice.status), "reconnected");
                send_message(server, room->players[1 - seat].connection,
                             &notice);
                log_line(server, "room %s player %d reconnected", room->code,
                         seat + 1);
                return;
            }
        }
    }
    send_error(server, connection, "rejoin_failed");
}

static void handle_create(NetServer *server, int connection,
                          NetMessage *message)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room >= 0) {
        send_error(server, connection, "already_in_room");
        return;
    }
    int index = -1;
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_ROOMS; ++i) {
        if (!server->rooms[i].used) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        send_error(server, connection, "server_full");
        return;
    }
    ServerRoom *room = &server->rooms[index];
    room_reset(room);
    char generated[REVERSI_NET_CODE_LEN + 1];
    if (!generate_code(server, generated)) {
        send_error(server, connection, "server_busy");
        return;
    }
    room->used = true;
    snprintf(room->code, sizeof(room->code), "%s", generated);
    room->players[0].seated = true;
    room->players[0].color = message->color;
    room->players[0].connection = connection;
    generate_token(server, room->players[0].token);
    conn->room = index;
    conn->seat = 0;
    room->expires = now_ms() + REVERSI_NET_SERVER_ROOM_WAIT_MS;
    NetMessage welcome = {
        .type = NET_MSG_WELCOME,
        .version = REVERSI_NET_VERSION,
        .player = 1
    };
    snprintf(welcome.token, sizeof(welcome.token), "%s",
             room->players[0].token);
    send_message(server, connection, &welcome);
    room_send_room(server, room, 0);
    log_line(server, "room %s created", room->code);
}

static void handle_join(NetServer *server, int connection,
                        NetMessage *message)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room >= 0) {
        send_error(server, connection, "already_in_room");
        return;
    }
    const int index = find_room(server, message->code);
    if (index < 0) {
        send_error(server, connection, "room_not_found");
        return;
    }
    ServerRoom *room = &server->rooms[index];
    if (room->playing || room->finished) {
        send_error(server, connection, "room_in_progress");
        return;
    }
    if (!room->players[0].seated || !room->players[1].seated) {
        const int seat = room->players[0].seated ? 1 : 0;
        if (seat == 0) {
            send_error(server, connection, "room_closed");
            return;
        }
        if (message->color == room->players[0].color) {
            send_error(server, connection, "color_taken");
            return;
        }
        room->players[seat].seated = true;
        room->players[seat].color = message->color;
        room->players[seat].connection = connection;
        generate_token(server, room->players[seat].token);
        conn->room = index;
        conn->seat = seat;
        NetMessage welcome = {
            .type = NET_MSG_WELCOME,
            .version = REVERSI_NET_VERSION,
            .player = seat + 1
        };
        snprintf(welcome.token, sizeof(welcome.token), "%s",
                 room->players[seat].token);
        send_message(server, connection, &welcome);
        room_send_room(server, room, 0);
        room_send_room(server, room, 1);
        log_line(server, "room %s joined", room->code);
        return;
    }
    send_error(server, connection, "room_full");
}

static void handle_ready(NetServer *server, int connection)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room < 0) {
        send_error(server, connection, "not_in_room");
        return;
    }
    ServerRoom *room = &server->rooms[conn->room];
    const int seat = room_seat_for_connection(room, connection);
    if (seat < 0 || room->playing || room->finished) {
        send_error(server, connection, "not_ready_state");
        return;
    }
    room->players[seat].ready = true;
    room_send_room(server, room, 0);
    room_send_room(server, room, 1);
    if (room->players[0].seated && room->players[1].seated
        && room->players[0].ready && room->players[1].ready
        && room->players[0].connection >= 0
        && room->players[1].connection >= 0) {
        start_game(server, conn->room);
    }
}

static void handle_resign(NetServer *server, int connection)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room < 0 || !server->rooms[conn->room].playing) {
        send_error(server, connection, "not_playing");
        return;
    }
    ServerRoom *room = &server->rooms[conn->room];
    const int seat = room_seat_for_connection(room, connection);
    if (seat < 0) {
        send_error(server, connection, "not_in_room");
        return;
    }
    const Stone winner = room->players[1 - seat].color;
    room_finish(server, conn->room,
                winner == STONE_BLACK ? GAME_RESULT_BLACK_WIN
                                      : GAME_RESULT_WHITE_WIN,
                "resign");
}

static void handle_leave(NetServer *server, int connection)
{
    ServerConnection *conn = &server->connections[connection];
    if (conn->room < 0) {
        send_error(server, connection, "not_in_room");
        return;
    }
    const int room_index = conn->room;
    const int seat = conn->seat;
    detach_player(server, room_index, seat, "left", true);
    conn->room = -1;
    conn->seat = -1;
}

static void dispatch(NetServer *server, int connection, NetMessage *message)
{
    switch (message->type) {
    case NET_MSG_HELLO:
        break;
    case NET_MSG_CREATE:
        handle_create(server, connection, message);
        break;
    case NET_MSG_JOIN:
        handle_join(server, connection, message);
        break;
    case NET_MSG_READY:
        handle_ready(server, connection);
        break;
    case NET_MSG_MOVE:
        handle_move(server, connection, message);
        break;
    case NET_MSG_RESIGN:
        handle_resign(server, connection);
        break;
    case NET_MSG_REJOIN:
        handle_rejoin(server, connection, message);
        break;
    case NET_MSG_LEAVE:
        handle_leave(server, connection);
        break;
    case NET_MSG_PING: {
        const NetMessage pong = {.type = NET_MSG_PONG};
        send_message(server, connection, &pong);
        break;
    }
    default:
        send_error(server, connection, "unexpected_message");
        break;
    }
}

static void process_frames(NetServer *server, int index)
{
    ServerConnection *connection = &server->connections[index];
    while (connection->state == CONNECTION_OPEN
           && connection->input_length > 0) {
        NetWsFrame frame;
        size_t consumed = 0;
        const NetWsResult result = net_ws_parse(
            &connection->parser, connection->input, connection->input_length,
            true, &frame, &consumed);
        if (result == NET_WS_NEED_MORE) {
            if (consumed == 0) {
                break;
            }
            memmove(connection->input, connection->input + consumed,
                    connection->input_length - consumed);
            connection->input_length -= consumed;
            continue;
        }
        if (result == NET_WS_ERROR) {
            connection_schedule_close(server, index);
            return;
        }
        if (result == NET_WS_TEXT) {
            connection->tokens += (int)((now_ms() - connection->tokens_ms)
                                        * REVERSI_NET_SERVER_TOKEN_RATE / 1000);
            connection->tokens_ms = now_ms();
            if (connection->tokens > REVERSI_NET_SERVER_TOKEN_CAP) {
                connection->tokens = REVERSI_NET_SERVER_TOKEN_CAP;
            }
            if (connection->tokens <= 0) {
                send_error(server, index, "rate_limited");
                connection_schedule_close(server, index);
                return;
            }
            --connection->tokens;
            char text[REVERSI_NET_MAX_MESSAGE + 1];
            if (frame.length > REVERSI_NET_MAX_MESSAGE) {
                send_error(server, index, "message_too_large");
            } else {
                memcpy(text, frame.payload, frame.length);
                text[frame.length] = '\0';
                NetMessage message;
                if (!net_protocol_decode(text, NET_DIRECTION_CLIENT_TO_SERVER,
                                         &message)) {
                    send_error(server, index, "bad_message");
                } else {
                    dispatch(server, index, &message);
                }
            }
        } else if (result == NET_WS_PING) {
            uint8_t pong[160];
            const size_t length = net_ws_encode(
                0xA, frame.payload, frame.length, false, NULL, pong,
                sizeof(pong));
            if (length > 0) {
                connection_queue(server, index, pong, length);
            }
        } else if (result == NET_WS_PONG) {
            connection->last_active = now_ms();
        } else if (result == NET_WS_CLOSE) {
            const int room = connection->room;
            const int seat = connection->seat;
            if (room >= 0 && seat >= 0) {
                detach_player(server, room, seat, "disconnected", false);
            }
            connection_schedule_close(server, index);
            return;
        }
        memmove(connection->input, connection->input + consumed,
                connection->input_length - consumed);
        connection->input_length -= consumed;
        connection->last_active = now_ms();
    }
}

static void handle_handshake(NetServer *server, int index)
{
    ServerConnection *connection = &server->connections[index];
    char *end = NULL;
    for (size_t i = 0; i + 3 < connection->input_length; ++i) {
        if (memcmp(connection->input + i, "\r\n\r\n", 4) == 0) {
            end = (char *)connection->input + i + 4;
            break;
        }
    }
    if (end == NULL) {
        return;
    }
    const size_t header_length = (size_t)(end - (char *)connection->input);
    char key[25];
    if (!net_ws_parse_handshake((const char *)connection->input,
                                header_length, key)) {
        connection_schedule_close(server, index);
        return;
    }
    char response[256];
    if (!net_ws_build_handshake_response(key, response)
        || !connection_queue(server, index, (const uint8_t *)response,
                             strlen(response))) {
        connection_schedule_close(server, index);
        return;
    }
    connection->state = CONNECTION_OPEN;
    connection->last_active = now_ms();
    memmove(connection->input, connection->input + header_length,
            connection->input_length - header_length);
    connection->input_length -= header_length;
    connection_flush(server, index);
    process_frames(server, index);
}

static void read_connection(NetServer *server, int index)
{
    ServerConnection *connection = &server->connections[index];
    uint8_t buffer[REVERSI_NET_SERVER_READ_CAP];
    for (;;) {
        const int read = socket_read(connection->socket, buffer,
                                     sizeof(buffer));
        if (read == 0) {
            const int room = connection->room;
            const int seat = connection->seat;
            if (room >= 0 && seat >= 0 && server->rooms[room].used) {
                detach_player(server, room, seat, "disconnected", false);
            }
            connection_schedule_close(server, index);
            return;
        }
        if (read < 0) {
            break;
        }
        if (connection->input_length + (size_t)read
            > sizeof(connection->input)) {
            connection_schedule_close(server, index);
            return;
        }
        memcpy(connection->input + connection->input_length, buffer,
               (size_t)read);
        connection->input_length += (size_t)read;
        connection->last_active = now_ms();
        if (connection->state == CONNECTION_HANDSHAKE) {
            handle_handshake(server, index);
        } else {
            process_frames(server, index);
        }
        if (connection->state == CONNECTION_FREE) {
            return;
        }
        if (connection->input_length == sizeof(connection->input)) {
            connection_schedule_close(server, index);
            return;
        }
    }
}

static void accept_connections(NetServer *server)
{
    for (;;) {
        struct sockaddr_storage address;
        NetSockLen length = (NetSockLen)sizeof(address);
        const NetSocket socket = accept(
            server->listener, (struct sockaddr *)&address, &length);
        if (socket == NET_INVALID) {
            return;
        }
        int index = -1;
        for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
            if (server->connections[i].state == CONNECTION_FREE) {
                index = i;
                break;
            }
        }
        if (index < 0 || !socket_nonblocking(socket)) {
            socket_close(socket);
            continue;
        }
        ServerConnection *connection = &server->connections[index];
        memset(connection, 0, sizeof(*connection));
        connection->state = CONNECTION_HANDSHAKE;
        connection->socket = socket;
        connection->handshake_deadline = now_ms() + REVERSI_NET_SERVER_HANDSHAKE_MS;
        connection->last_active = now_ms();
        connection->tokens = REVERSI_NET_SERVER_TOKEN_CAP;
        connection->tokens_ms = now_ms();
        connection->room = -1;
        connection->seat = -1;
        net_ws_parser_init(&connection->parser);
    }
}

NetServer *net_server_create(const NetServerConfig *config)
{
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return NULL;
    }
#endif
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    NetServer *server = calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->config = config != NULL ? *config : (NetServerConfig){0};
    if (server->config.bind_address == NULL) {
        server->config.bind_address = "127.0.0.1";
    }
    if (server->config.move_timeout_ms <= 0) {
        server->config.move_timeout_ms = 120000;
    }
    if (server->config.reconnect_timeout_ms <= 0) {
        server->config.reconnect_timeout_ms = 60000;
    }
    server->listener = NET_INVALID;
    server->port = 0;
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
        server->connections[i].socket = NET_INVALID;
        server->connections[i].room = -1;
        server->connections[i].seat = -1;
    }
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_ROOMS; ++i) {
        room_reset(&server->rooms[i]);
    }
    uint8_t seed[8];
    if (os_random(seed, sizeof(seed))) {
        memcpy(&server->rng, seed, sizeof(server->rng));
    } else {
        server->rng = now_ms() ^ 0x9E3779B97F4A7C15ULL;
    }
    if (server->rng == 0) {
        server->rng = 1;
    }
    return server;
}

bool net_server_start(NetServer *server, char *error, size_t error_size)
{
    if (server == NULL) {
        return false;
    }
    server->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listener == NET_INVALID) {
        snprintf(error, error_size, "cannot create socket");
        return false;
    }
    int reuse = 1;
    setsockopt(server->listener, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)server->config.port);
    if (inet_pton(AF_INET, server->config.bind_address,
                  &address.sin_addr) != 1) {
        snprintf(error, error_size, "invalid bind address");
        socket_close(server->listener);
        server->listener = NET_INVALID;
        return false;
    }
    if (bind(server->listener, (struct sockaddr *)&address,
             sizeof(address)) != 0) {
        snprintf(error, error_size, "cannot bind");
        socket_close(server->listener);
        server->listener = NET_INVALID;
        return false;
    }
    if (listen(server->listener, 16) != 0
        || !socket_nonblocking(server->listener)) {
        snprintf(error, error_size, "cannot listen");
        socket_close(server->listener);
        server->listener = NET_INVALID;
        return false;
    }
    NetSockLen length = (NetSockLen)sizeof(address);
    if (getsockname(server->listener, (struct sockaddr *)&address,
                    &length) == 0) {
        server->port = ntohs(address.sin_port);
    }
    server->started = true;
    return true;
}

int net_server_port(const NetServer *server)
{
    return server != NULL ? server->port : 0;
}

static void server_timers(NetServer *server)
{
    const uint64_t now = now_ms();
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
        ServerConnection *connection = &server->connections[i];
        if (connection->state == CONNECTION_HANDSHAKE
            && now >= connection->handshake_deadline) {
            connection_schedule_close(server, i);
        } else if (connection->state == CONNECTION_OPEN
                   && now - connection->last_active > REVERSI_NET_SERVER_IDLE_MS) {
            connection_schedule_close(server, i);
        }
    }
    for (int r = 0; r < REVERSI_NET_SERVER_MAX_ROOMS; ++r) {
        ServerRoom *room = &server->rooms[r];
        if (!room->used) {
            continue;
        }
        for (int seat = 0; seat < 2; ++seat) {
            ServerPlayer *player = &room->players[seat];
            if (player->seated && player->connection < 0
                && player->rejoin_deadline != 0
                && now >= player->rejoin_deadline) {
                const int other = 1 - seat;
                if (room->players[other].connection < 0) {
                    room_close(server, r, "abandoned");
                } else if (room->playing) {
                    const Stone winner = room->players[other].color;
                    room_finish(server, r,
                                winner == STONE_BLACK
                                    ? GAME_RESULT_BLACK_WIN
                                    : GAME_RESULT_WHITE_WIN,
                                "disconnect");
                } else {
                    room_close(server, r, "abandoned");
                }
                break;
            }
        }
        if (!room->used) {
            continue;
        }
        if (room->playing && now >= room->move_deadline) {
            const int connected0 = room->players[0].connection >= 0;
            const int connected1 = room->players[1].connection >= 0;
            if (!connected0 && !connected1) {
                room_close(server, r, "abandoned");
            } else {
                /* The player to move loses on the clock. */
                const int to_move =
                    room->players[0].color == room->game.current_player ? 0 : 1;
                const int winner = 1 - to_move;
                room_finish(server, r,
                            room->players[winner].color == STONE_BLACK
                                ? GAME_RESULT_BLACK_WIN
                                : GAME_RESULT_WHITE_WIN,
                            "timeout");
            }
        } else if (!room->playing && room->expires != 0
                   && now >= room->expires) {
            room_close(server, r, "expired");
        }
    }
}

void net_server_poll(NetServer *server, int timeout_ms)
{
    if (server == NULL || !server->started) {
        return;
    }
    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_SET(server->listener, &read_set);
    NetSocket maximum = server->listener;
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
        ServerConnection *connection = &server->connections[i];
        if (connection->state == CONNECTION_FREE) {
            continue;
        }
        FD_SET(connection->socket, &read_set);
        if (connection->output_length > 0) {
            FD_SET(connection->socket, &write_set);
        }
        if (connection->socket > maximum) {
            maximum = connection->socket;
        }
    }
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    const int ready = select((int)maximum + 1, &read_set, &write_set,
                             NULL, &timeout);
    if (ready > 0) {
        if (FD_ISSET(server->listener, &read_set)) {
            accept_connections(server);
        }
        for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
            ServerConnection *connection = &server->connections[i];
            if (connection->state == CONNECTION_FREE) {
                continue;
            }
            if (FD_ISSET(connection->socket, &read_set)) {
                read_connection(server, i);
            }
            if (connection->state != CONNECTION_FREE
                && FD_ISSET(connection->socket, &write_set)) {
                connection_flush(server, i);
            }
        }
    }
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
        if (server->connections[i].state != CONNECTION_FREE) {
            connection_flush(server, i);
        }
    }
    server_timers(server);
}

void net_server_destroy(NetServer *server)
{
    if (server == NULL) {
        return;
    }
    if (server->listener != NET_INVALID) {
        socket_close(server->listener);
    }
    for (int i = 0; i < REVERSI_NET_SERVER_MAX_CONNECTIONS; ++i) {
        if (server->connections[i].socket != NET_INVALID) {
            socket_close(server->connections[i].socket);
        }
    }
    free(server);
#if defined(_WIN32)
    WSACleanup();
#endif
}
