#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_protocol.h"
#include "net/net_server.h"
#include "net/net_ws.h"
#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET TestSocket;
#define TEST_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int TestSocket;
#define TEST_INVALID (-1)
#endif

typedef struct TestClient {
    TestSocket socket;
    NetWsParser parser;
    uint8_t input[8192];
    size_t input_length;
    uint64_t rng;
    char token[REVERSI_NET_TOKEN_LEN + 1];
} TestClient;

static void sleep_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void test_close(TestClient *client)
{
    if (client->socket != TEST_INVALID) {
#if defined(_WIN32)
        closesocket(client->socket);
#else
        close(client->socket);
#endif
        client->socket = TEST_INVALID;
    }
}

static bool test_connect(NetServer *server, TestClient *client, int port)
{
    memset(client, 0, sizeof(*client));
    client->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket == TEST_INVALID) {
        return false;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(client->socket, (struct sockaddr *)&address,
                sizeof(address)) != 0) {
        test_close(client);
        return false;
    }
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(client->socket, FIONBIO, &mode);
#else
    const int flags = fcntl(client->socket, F_GETFL, 0);
    fcntl(client->socket, F_SETFL, flags | O_NONBLOCK);
#endif
    client->rng = 0x9E3779B97F4A7C15ULL;
    char key[25];
    if (!net_ws_generate_key(&client->rng, key)) {
        return false;
    }
    char request[256];
    if (!net_ws_build_handshake_request("127.0.0.1", port, key, request)) {
        return false;
    }
    if (send(client->socket, request, (int)strlen(request), 0)
        != (int)strlen(request)) {
        return false;
    }
    char response[512];
    size_t used = 0;
    for (int attempt = 0; attempt < 200 && used < sizeof(response) - 1;
         ++attempt) {
        const int read = (int)recv(client->socket, response + used,
                                   (int)(sizeof(response) - 1 - used), 0);
        if (read > 0) {
            used += (size_t)read;
            response[used] = '\0';
            if (strstr(response, "\r\n\r\n") != NULL) {
                break;
            }
        } else if (read < 0) {
            net_server_poll(server, 1);
        } else {
            return false;
        }
    }
    if (!net_ws_validate_handshake_response(response, used, key)) {
        return false;
    }
    net_ws_parser_init(&client->parser);
    return true;
}

static bool test_send(TestClient *client, const NetMessage *message)
{
    char text[REVERSI_NET_MAX_MESSAGE + 1];
    if (net_protocol_encode(message, text, sizeof(text)) == 0) {
        return false;
    }
    uint8_t frame[REVERSI_NET_MAX_MESSAGE + 20];
    const size_t length = net_ws_encode(
        0x1, (const uint8_t *)text, strlen(text), true, &client->rng, frame,
        sizeof(frame));
    if (length == 0) {
        return false;
    }
    size_t sent = 0;
    while (sent < length) {
        const int written = (int)send(client->socket, (const char *)frame + sent,
                                      (int)(length - sent), 0);
        if (written <= 0) {
            return false;
        }
        sent += (size_t)written;
    }
    return true;
}

static void test_read_available(TestClient *client)
{
    uint8_t buffer[4096];
    for (;;) {
        const int read = (int)recv(client->socket, (char *)buffer,
                                   (int)sizeof(buffer), 0);
        if (read <= 0) {
            return;
        }
        if (client->input_length + (size_t)read > sizeof(client->input)) {
            return;
        }
        memcpy(client->input + client->input_length, buffer, (size_t)read);
        client->input_length += (size_t)read;
    }
}

static bool parse_queued(TestClient *client, NetMessage *message)
{
    for (;;) {
        NetWsFrame frame;
        size_t consumed = 0;
        const NetWsResult result = net_ws_parse(
            &client->parser, client->input, client->input_length, false,
            &frame, &consumed);
        if (result == NET_WS_NEED_MORE) {
            if (consumed == 0) {
                return false;
            }
        } else if (result == NET_WS_TEXT) {
            char text[REVERSI_NET_MAX_MESSAGE + 1];
            if (frame.length > REVERSI_NET_MAX_MESSAGE) {
                return false;
            }
            memcpy(text, frame.payload, frame.length);
            text[frame.length] = '\0';
            memmove(client->input, client->input + consumed,
                    client->input_length - consumed);
            client->input_length -= consumed;
            return net_protocol_decode(text, NET_DIRECTION_SERVER_TO_CLIENT,
                                       message);
        } else {
            return false;
        }
        memmove(client->input, client->input + consumed,
                client->input_length - consumed);
        client->input_length -= consumed;
    }
}

static bool client_next(NetServer *server, TestClient *client,
                        NetMessage *message, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 2) {
        net_server_poll(server, 1);
        if (parse_queued(client, message)) {
            return true;
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client->socket, &read_set);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 2000};
        if (select((int)client->socket + 1, &read_set, NULL, NULL,
                   &timeout) > 0) {
            test_read_available(client);
        }
    }
    return false;
}

static bool client_expect(NetServer *server, TestClient *client,
                          NetMessageType type, NetMessage *message,
                          int timeout_ms)
{
    NetMessage current;
    while (client_next(server, client, &current, timeout_ms)) {
        if (getenv("NET_TEST_DEBUG") != NULL
            && current.type != type) {
            fprintf(stderr, "unexpected %s error=%s status=%s\n",
                    net_message_type_name(current.type), current.error,
                    current.status);
        }
        if (current.type == type) {
            if (message != NULL) {
                *message = current;
            }
            return true;
        }
    }
    return false;
}

static bool pump_both(NetServer *server, TestClient *a, TestClient *b,
                      NetMessage *message_a, NetMessage *message_b,
                      NetMessageType type, int timeout_ms)
{
    const bool ok_a = client_expect(server, a, type, message_a, timeout_ms);
    const bool ok_b = client_expect(server, b, type, message_b, timeout_ms);
    return ok_a && ok_b;
}

static void test_normal_game(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 5000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, server != NULL);
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);

    TestClient a;
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));

    NetMessage message;
    REVERSI_TEST_ASSERT(stats, test_send(&a, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    REVERSI_TEST_ASSERT(stats, test_send(&a, &(NetMessage){
        .type = NET_MSG_CREATE, .color = STONE_BLACK}));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    snprintf(a.token, sizeof(a.token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);

    REVERSI_TEST_ASSERT(stats, test_send(&b, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    REVERSI_TEST_ASSERT(stats, test_send(&b, &join));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
    snprintf(b.token, sizeof(b.token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));

    REVERSI_TEST_ASSERT(stats, test_send(&a, &(NetMessage){
        .type = NET_MSG_READY}));
    REVERSI_TEST_ASSERT(stats, test_send(&b, &(NetMessage){
        .type = NET_MSG_READY}));
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, 3000));
    REVERSI_TEST_ASSERT(stats, start_a.seq == 0 && start_b.seq == 0);
    REVERSI_TEST_ASSERT(stats, start_a.turn == STONE_BLACK);

    GameState local;
    game_init(&local);
    int seq = 0;
    bool finished = false;
    for (int ply = 0; ply < 80 && !finished; ++ply) {
        const bool black_turn = local.current_player == STONE_BLACK;
        TestClient *mover = black_turn ? &a : &b;
        TestClient *other = black_turn ? &b : &a;
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &local.board, local.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            break;
        }
        NetMessage move = {.type = NET_MSG_MOVE, .seq = seq,
                           .last = moves[0], .has_last = true};
        REVERSI_TEST_ASSERT(stats, test_send(mover, &move));
        NetMessage state_a;
        NetMessage state_b;
        REVERSI_TEST_ASSERT(stats, client_expect(server, mover, NET_MSG_STATE,
                                                 &state_a, 3000));
        REVERSI_TEST_ASSERT(stats, client_expect(server, other, NET_MSG_STATE,
                                                 &state_b, 3000));
        REVERSI_TEST_ASSERT(stats, state_a.seq == seq + 1);
        REVERSI_TEST_ASSERT(stats, state_b.seq == seq + 1);
        REVERSI_TEST_ASSERT(stats, state_a.has_last
                            && state_a.last.row == moves[0].row
                            && state_a.last.col == moves[0].col);
        GameState expected = local;
        REVERSI_TEST_ASSERT(stats, game_apply_move(&expected, moves[0]));
        GameState transmitted;
        REVERSI_TEST_ASSERT(stats, net_protocol_state_from_board(
            state_a.board, state_a.turn, state_a.result, &transmitted));
        REVERSI_TEST_ASSERT(stats, game_state_equal(&transmitted, &expected));
        local = expected;
        ++seq;
        if (local.result != GAME_RESULT_ONGOING) {
            NetMessage over_a;
            NetMessage over_b;
            REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_GAMEOVER,
                                                     &over_a, 3000));
            REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_GAMEOVER,
                                                     &over_b, 3000));
            REVERSI_TEST_ASSERT(stats, over_a.result == local.result);
            finished = true;
        }
    }
    REVERSI_TEST_ASSERT(stats, finished);

    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}

static void test_illegal_and_stale(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE,
                                .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_START,
                                             &message, 3000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_START,
                                             &message, 3000));

    /* White tries to move first. */
    NetMessage early = {.type = NET_MSG_MOVE, .seq = 0,
                        .last = {5, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, test_send(&b, &early));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "not_your_turn") == 0);

    /* Black plays an occupied square. */
    NetMessage illegal = {.type = NET_MSG_MOVE, .seq = 0,
                          .last = {3, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, test_send(&a, &illegal));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "illegal_move") == 0);

    /* Correct move, then a duplicate with the old seq. */
    NetMessage legal = {.type = NET_MSG_MOVE, .seq = 0,
                        .last = {5, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, test_send(&a, &legal));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_STATE,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, message.seq == 1);
    REVERSI_TEST_ASSERT(stats, test_send(&a, &legal));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "not_your_turn") == 0
                        || strcmp(message.error, "stale_state") == 0);
    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}

static void test_reconnect_and_leave(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .reconnect_timeout_ms = 2000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    TestClient b;
    test_connect(server, &a, port);
    test_connect(server, &b, port);
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE,
                                .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    char token[33];
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));

    /* Host leaves while waiting: the guest is told the room closed. */
    test_send(&a, &(NetMessage){.type = NET_MSG_LEAVE});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOMCLOSED,
                                             &message, 2000));

    /* A new room for reconnect. */
    TestClient c;
    test_connect(server, &c, port);
    test_send(&c, &(NetMessage){.type = NET_MSG_CREATE,
                                .color = STONE_WHITE});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_WELCOME,
                                             &message, 2000));
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_ROOM,
                                             &message, 2000));
    snprintf(code, sizeof(code), "%s", message.code);
    TestClient d;
    test_connect(server, &d, port);
    NetMessage join2 = {.type = NET_MSG_JOIN, .color = STONE_BLACK};
    snprintf(join2.code, sizeof(join2.code), "%s", code);
    test_send(&d, &join2);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&c, &(NetMessage){.type = NET_MSG_READY});
    test_send(&d, &(NetMessage){.type = NET_MSG_READY});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_START,
                                             &message, 3000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_START,
                                             &message, 3000));

    test_close(&c);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "disconnected") == 0);
    TestClient c2;
    test_connect(server, &c2, port);
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token);
    test_send(&c2, &rejoin);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c2, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c2, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c2, NET_MSG_STATE,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "reconnected") == 0);
    test_close(&c2);
    test_close(&d);
    test_close(&b);
    net_server_destroy(server);
}

static void test_fuzz_survives(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient fuzz;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &fuzz, port));
    uint64_t rng = 0x12345678;
    for (int i = 0; i < 200; ++i) {
        uint8_t data[128];
        const size_t length = (size_t)(rng % sizeof(data)) + 1;
        for (size_t j = 0; j < length; ++j) {
            rng = rng * 6364136223846793005ULL + 1;
            data[j] = (uint8_t)(rng >> 33);
        }
        (void)send(fuzz.socket, (const char *)data, (int)length, 0);
        net_server_poll(server, 1);
    }
    sleep_ms(20);
    net_server_poll(server, 1);
    /* A valid client can still use the server. */
    TestClient good;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &good, port));
    test_send(&good, &(NetMessage){.type = NET_MSG_CREATE,
                                   .color = STONE_BLACK});
    NetMessage message;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &good, NET_MSG_WELCOME,
                                             &message, 2000));
    test_close(&fuzz);
    test_close(&good);
    net_server_destroy(server);
}

static void test_multi_room(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a, b, c, d;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &c, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &d, port));
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    test_send(&c, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_WHITE});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code_a[7];
    snprintf(code_a, sizeof(code_a), "%s", message.code);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_ROOM,
                                             &message, 2000));
    char code_c[7];
    snprintf(code_c, sizeof(code_c), "%s", message.code);
    REVERSI_TEST_ASSERT(stats, strcmp(code_a, code_c) != 0);
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code_a);
    test_send(&b, &join);
    join.color = STONE_BLACK;
    snprintf(join.code, sizeof(join.code), "%s", code_c);
    test_send(&d, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.code, code_a) == 0);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.code, code_c) == 0);
    test_close(&a);
    test_close(&b);
    test_close(&c);
    test_close(&d);
    net_server_destroy(server);
}

static void test_server_restart(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *first = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(first, error, sizeof(error)));
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(first, &a,
                                            net_server_port(first)));
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(first, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);
    net_server_destroy(first);
    sleep_ms(10);

    NetServer *second = net_server_create(&config);
    REVERSI_TEST_ASSERT(stats, net_server_start(second, error, sizeof(error)));
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(second, &b,
                                            net_server_port(second)));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(second, &b, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "room_not_found") == 0);
    test_close(&a);
    test_close(&b);
    net_server_destroy(second);
}

int main(void)
{
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_normal_game);
    REVERSI_RUN_TEST(&stats, test_illegal_and_stale);
    REVERSI_RUN_TEST(&stats, test_reconnect_and_leave);
    REVERSI_RUN_TEST(&stats, test_multi_room);
    REVERSI_RUN_TEST(&stats, test_server_restart);
    REVERSI_RUN_TEST(&stats, test_fuzz_survives);
    return reversi_test_summary(&stats, "net.server");
}
