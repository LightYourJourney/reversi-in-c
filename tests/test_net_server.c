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
    bool closed;
    uint64_t rng;
    char token[REVERSI_NET_TOKEN_LEN + 1];
} TestClient;

static bool test_read_would_block(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

/* Extra work (a simulated network link) pumped on every poll slice. */
static void (*g_pump_hook)(void) = NULL;

static void pump_extra(void)
{
    if (g_pump_hook != NULL) {
        g_pump_hook();
    }
}

static uint64_t test_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static void test_sleep_ms(int ms)
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

static void test_nonblocking(TestSocket socket_fd)
{
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(socket_fd, FIONBIO, &mode);
#else
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void test_close_socket(TestSocket socket_fd)
{
#if defined(_WIN32)
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

static bool test_connect_buffer(NetServer *server, TestClient *client,
                                int port, int receive_buffer)
{
    memset(client, 0, sizeof(*client));
    client->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket == TEST_INVALID) {
        return false;
    }
    if (receive_buffer > 0) {
        setsockopt(client->socket, SOL_SOCKET, SO_RCVBUF,
                   (const char *)&receive_buffer, sizeof(receive_buffer));
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
    for (int attempt = 0; attempt < 400 && used < sizeof(response) - 1;
         ++attempt) {
        pump_extra();
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

static bool test_connect(NetServer *server, TestClient *client, int port)
{
    return test_connect_buffer(server, client, port, 0);
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
        if (read == 0) {
            client->closed = true;
            return;
        }
        if (read < 0) {
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
        pump_extra();
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

static bool test_wait_closed(NetServer *server, TestClient *client,
                             int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 2) {
        net_server_poll(server, 1);
        pump_extra();
        if (client->closed) {
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
    return client->closed;
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
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&c, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&d, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&c2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_sleep_ms(20);
    net_server_poll(server, 1);
    /* A valid client can still use the server. */
    TestClient good;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &good, port));
    test_send(&good, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&c, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&d, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(first, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);
    net_server_destroy(first);
    test_sleep_ms(10);

    NetServer *second = net_server_create(&config);
    REVERSI_TEST_ASSERT(stats, net_server_start(second, error, sizeof(error)));
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(second, &b,
                                            net_server_port(second)));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(second, &b, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "room_not_found") == 0);
    test_close(&a);
    test_close(&b);
    net_server_destroy(second);
}

static void test_move_timeout(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 300,
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
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    REVERSI_TEST_ASSERT(stats, test_send(&b, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    REVERSI_TEST_ASSERT(stats, test_send(&b, &join));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
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
    REVERSI_TEST_ASSERT(stats, start_a.turn == STONE_BLACK);

    /* Black (seat 0) lets the clock run out: White must win. */
    NetMessage over_a;
    NetMessage over_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &over_a, &over_b,
                                         NET_MSG_GAMEOVER, 3000));
    REVERSI_TEST_ASSERT(stats, over_a.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, over_b.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, strcmp(over_a.status, "timeout") == 0);
    REVERSI_TEST_ASSERT(stats, strcmp(over_b.status, "timeout") == 0);

    /* White (seat 1) stalls after Black's move: Black must win. */
    TestClient c;
    TestClient d;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &c, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &d, port));
    REVERSI_TEST_ASSERT(stats, test_send(&c, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    REVERSI_TEST_ASSERT(stats, test_send(&c, &(NetMessage){
        .type = NET_MSG_CREATE, .color = STONE_BLACK}));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join2 = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join2.code, sizeof(join2.code), "%s", message.code);
    REVERSI_TEST_ASSERT(stats, test_send(&d, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    REVERSI_TEST_ASSERT(stats, test_send(&d, &join2));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &d, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, test_send(&c, &(NetMessage){
        .type = NET_MSG_READY}));
    REVERSI_TEST_ASSERT(stats, test_send(&d, &(NetMessage){
        .type = NET_MSG_READY}));
    NetMessage start_c;
    NetMessage start_d;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &c, &d, &start_c, &start_d,
                                         NET_MSG_START, 3000));
    const NetMessage move = {.type = NET_MSG_MOVE, .seq = 0,
                             .last = {5, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, test_send(&c, &move));
    NetMessage state_c;
    NetMessage state_d;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &c, &d, &state_c, &state_d,
                                         NET_MSG_STATE, 3000));
    net_server_poll(server, 200);
    net_server_poll(server, 200);
    NetMessage late_c;
    NetMessage late_d;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &c, &d, &late_c, &late_d,
                                         NET_MSG_GAMEOVER, 3000));
    if (late_c.result != GAME_RESULT_BLACK_WIN
        || late_d.result != GAME_RESULT_BLACK_WIN) {
        fprintf(stderr, "white-stall timeout: c=%d/%s d=%d/%s\n",
                (int)late_c.result, late_c.status,
                (int)late_d.result, late_d.status);
    }
    REVERSI_TEST_ASSERT(stats, late_c.result == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, late_d.result == GAME_RESULT_BLACK_WIN);

    test_close(&a);
    test_close(&b);
    test_close(&c);
    test_close(&d);
    net_server_destroy(server);
}

static void test_rate_limit(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a,
                                            net_server_port(server)));
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    for (int i = 0; i < 45; ++i) {
        test_send(&a, &(NetMessage){.type = NET_MSG_PING});
    }
    NetMessage message;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "rate_limited") == 0);
    REVERSI_TEST_ASSERT(stats, test_wait_closed(server, &a, 2000));
    test_close(&a);
    net_server_destroy(server);
}

static void test_rejoin_timeout(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 100, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    char token[REVERSI_NET_TOKEN_LEN + 1];
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, 3000));

    test_close(&a);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "disconnected") == 0);
    NetMessage over;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_GAMEOVER,
                                             &over, 3000));
    REVERSI_TEST_ASSERT(stats, over.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, strcmp(over.status, "disconnect") == 0);

    /* After the window closes the old token must be rejected. */
    TestClient a2;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a2, port));
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token);
    test_send(&a2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a2, &rejoin);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "rejoin_failed") == 0);
    test_close(&a2);

    test_close(&b);
    net_server_destroy(server);
}

static void test_room_full(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    TestClient b;
    TestClient c;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &c, port));

    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&c, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    join.color = STONE_BLACK;
    test_send(&c, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &c, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "room_full") == 0);

    test_close(&a);
    test_close(&b);
    test_close(&c);
    net_server_destroy(server);
}

static void test_rejoin_finished(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 5000, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    char token[REVERSI_NET_TOKEN_LEN + 1];
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, 3000));

    test_close(&a);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_OPPONENT,
                                             &message, 2000));
    test_send(&b, &(NetMessage){.type = NET_MSG_RESIGN});
    NetMessage over_b;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_GAMEOVER,
                                             &over_b, 2000));
    REVERSI_TEST_ASSERT(stats, over_b.result == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, strcmp(over_b.status, "resign") == 0);

    TestClient a2;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a2, port));
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token);
    test_send(&a2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a2, &rejoin);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "finished") == 0);
    NetMessage state;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_STATE,
                                             &state, 2000));
    REVERSI_TEST_ASSERT(stats, state.result == GAME_RESULT_BLACK_WIN);
    NetMessage over;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_GAMEOVER,
                                             &over, 2000));
    REVERSI_TEST_ASSERT(stats, over.result == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, strcmp(over.status, "resign") == 0);

    test_close(&a2);
    test_close(&b);
    net_server_destroy(server);
}

static TestSocket test_connect_raw(int port)
{
    TestSocket socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == TEST_INVALID) {
        return TEST_INVALID;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(socket_fd, (struct sockaddr *)&address,
                sizeof(address)) != 0) {
        test_close_socket(socket_fd);
        return TEST_INVALID;
    }
    test_nonblocking(socket_fd);
    return socket_fd;
}

/* Pumps the server until the wall clock passes the deadline, so timing
 * assertions do not depend on how fast the harness loop happens to run. */
static void test_pump_until(NetServer *server, uint64_t deadline)
{
    while (test_now_ms() < deadline) {
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
}

static void test_pump(NetServer *server, int milliseconds)
{
    test_pump_until(server, test_now_ms() + (uint64_t)milliseconds);
}

static void test_hello_order(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));

    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE,
                                .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "hello_required") == 0);

    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error,
                                      "unexpected_message") == 0);

    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE,
                                .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    test_close(&a);
    net_server_destroy(server);
}

static void test_handshake_timeout(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .handshake_timeout_ms = 100, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const TestSocket raw = test_connect_raw(net_server_port(server));
    REVERSI_TEST_ASSERT(stats, raw != TEST_INVALID);
    bool closed = false;
    char byte;
    for (int elapsed = 0; elapsed < 2000 && !closed; elapsed += 5) {
        net_server_poll(server, 1);
        const int result = (int)recv(raw, &byte, 1, 0);
        if (result == 0) {
            closed = true;
        } else if (result < 0 && !test_read_would_block()) {
            closed = true;
        }
        test_sleep_ms(5);
    }
    REVERSI_TEST_ASSERT(stats, closed);
    test_close_socket(raw);
    net_server_destroy(server);
}

static void test_idle_timeout(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .idle_timeout_ms = 300, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    /* Nothing else is sent: the idle timer must reap the connection. */
    REVERSI_TEST_ASSERT(stats, test_wait_closed(server, &a, 3000));
    test_close(&a);
    net_server_destroy(server);
}

static void test_room_ttl(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .room_wait_ms = 400, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);

    /* The waiting-room TTL closes the room and tells both seats. */
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOMCLOSED,
                                             &message, 3000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "expired") == 0);

    /* The code is gone for good. */
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error,
                                      "room_not_found") == 0);
    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}

static void test_room_ttl_refresh(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .room_wait_ms = 3000, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);

    /* Join late in the original window; the room must not expire on the
     * creation timestamp while both players are present. The margins are
     * ~1.8 s before the original deadline and ~0.9 s before the refreshed
     * one, so a slow runner does not decide the result. */
    const uint64_t created = test_now_ms();
    test_pump_until(server, created + 1200);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_pump_until(server, created + 3300);
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_START,
                                             &message, 3000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_START,
                                             &message, 3000));
    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}

static void test_waiting_room_reconnect(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .reconnect_timeout_ms = 3000, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    char token[33];
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));

    /* A dropped socket in the waiting room must keep the seat instead of
     * destroying the room for the player who is still waiting. */
    test_close(&a);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "disconnected") == 0);

    TestClient a2;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a2, port));
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token);
    test_send(&a2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a2, &rejoin);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_WELCOME,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_ROOM,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "waiting") == 0);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "reconnected") == 0);

    test_send(&a2, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_START,
                                             &message, 3000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_START,
                                             &message, 3000));
    test_close(&a2);
    test_close(&b);
    net_server_destroy(server);
}

static void test_rejoin_takeover(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 3000, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_WELCOME,
                                             &message, 2000));
    char token[33];
    snprintf(token, sizeof(token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, 3000));

    /* The old socket is still open (a NAT change looks like this to the
     * server); the token holder must be able to take its seat back. */
    TestClient a2;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a2, port));
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token);
    test_send(&a2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a2, &rejoin);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_WELCOME,
                                             &message, 2000));
    char new_token[33];
    snprintf(new_token, sizeof(new_token), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, strcmp(new_token, token) != 0);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage state;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_STATE,
                                             &state, 2000));
    REVERSI_TEST_ASSERT(stats, state.seq == 0);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_OPPONENT,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.status, "reconnected") == 0);

    /* The stale socket is closed and can no longer act for the seat. */
    REVERSI_TEST_ASSERT(stats, test_wait_closed(server, &a, 2000));

    NetMessage move = {.type = NET_MSG_MOVE, .seq = 0,
                       .last = {5, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, test_send(&a2, &move));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a2, NET_MSG_STATE,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, message.seq == 1);
    test_close(&a2);
    test_close(&b);
    net_server_destroy(server);
}

static void test_join_failure_limit(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "ZZZZZZ");
    NetMessage message;
    for (int i = 0; i < 7; ++i) {
        test_send(&a, &join);
        REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                                 &message, 2000));
        REVERSI_TEST_ASSERT(stats, strcmp(message.error,
                                          "room_not_found") == 0);
    }
    test_send(&a, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ERROR,
                                             &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "rate_limited") == 0);
    REVERSI_TEST_ASSERT(stats, test_wait_closed(server, &a, 2000));
    test_close(&a);
    net_server_destroy(server);
}

static void test_connection_cap(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient clients[48];
    int opened = 0;
    for (int i = 0; i < 48; ++i) {
        if (!test_connect(server, &clients[i], port)) {
            break;
        }
        ++opened;
    }
    REVERSI_TEST_ASSERT(stats, opened == 48);
    TestClient extra;
    REVERSI_TEST_ASSERT(stats, !test_connect(server, &extra, port));
    /* The rejected connection gets an HTTP-level 503, not a silent reset. */
    TestSocket raw = test_connect_raw(port);
    REVERSI_TEST_ASSERT(stats, raw != TEST_INVALID);
    uint64_t rng = 0x5151ULL;
    char key[25];
    char request[256];
    if (net_ws_generate_key(&rng, key)
        && net_ws_build_handshake_request("127.0.0.1", port, key, request)) {
        (void)send(raw, request, (int)strlen(request), 0);
    }
    /* Accumulate: the status line can arrive in more than one TCP segment
     * (observed on Windows). */
    char response[128] = {0};
    size_t used = 0;
    for (int elapsed = 0; elapsed < 2000 && used < 12; elapsed += 2) {
        net_server_poll(server, 1);
        const int read = (int)recv(raw, response + used,
                                   (int)(sizeof(response) - 1 - used), 0);
        if (read > 0) {
            used += (size_t)read;
            response[used] = '\0';
        } else if (read == 0) {
            break;
        }
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, used >= 12);
    REVERSI_TEST_ASSERT(stats, strncmp(response, "HTTP/1.1 503", 12) == 0);
    test_close_socket(raw);
    /* Existing connections keep working while the cap is in force. */
    NetMessage message;
    test_send(&clients[0], &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&clients[0], &(NetMessage){.type = NET_MSG_CREATE,
                                         .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &clients[0],
                                             NET_MSG_ROOM, &message, 2000));
    /* Freeing a slot lets a new connection through. */
    test_close(&clients[1]);
    test_pump(server, 100);
    TestClient fresh;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &fresh, port));
    for (int i = 0; i < 48; ++i) {
        if (i != 1) {
            test_close(&clients[i]);
        }
    }
    test_close(&fresh);
    net_server_destroy(server);
}

static void test_room_cap(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient clients[33];
    NetMessage message;
    int created = 0;
    for (int i = 0; i < 33; ++i) {
        if (!test_connect(server, &clients[i], port)) {
            break;
        }
        test_send(&clients[i], &(NetMessage){.type = NET_MSG_HELLO,
                                             .version = 1});
        test_send(&clients[i], &(NetMessage){.type = NET_MSG_CREATE,
                                             .color = STONE_BLACK});
        if (i < 32) {
            REVERSI_TEST_ASSERT(stats, client_expect(server, &clients[i],
                                                     NET_MSG_ROOM, &message,
                                                     2000));
            ++created;
        }
    }
    REVERSI_TEST_ASSERT(stats, created == 32);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &clients[32],
                                             NET_MSG_ERROR, &message, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(message.error, "server_full") == 0);

    /* Leaving the waiting room releases its slot for a new room. */
    test_send(&clients[0], &(NetMessage){.type = NET_MSG_LEAVE});
    test_send(&clients[32], &(NetMessage){.type = NET_MSG_CREATE,
                                          .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &clients[32],
                                             NET_MSG_ROOM, &message, 2000));
    for (int i = 0; i < 33; ++i) {
        test_close(&clients[i]);
    }
    net_server_destroy(server);
}

static void test_server_shutdown(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000, .quiet = true};
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
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, 3000));

    net_server_shutdown(server);
    NetMessage closed_a;
    NetMessage closed_b;
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOMCLOSED,
                                             &closed_a, 2000));
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOMCLOSED,
                                             &closed_b, 2000));
    REVERSI_TEST_ASSERT(stats, strcmp(closed_a.status, "shutdown") == 0);
    REVERSI_TEST_ASSERT(stats, strcmp(closed_b.status, "shutdown") == 0);
    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}

static void play_verified_game(TestStats *stats, NetServer *server,
                               int port, int timeout_ms);

/* --------------------------------------------------------------------
 * Delayed-network simulation.
 *
 * A byte-forwarding TCP proxy pumped from the test loop: every chunk is
 * held for DELAY_MS in each direction before it is forwarded, so both
 * peers observe round-trip latency without any kernel-level shaping.
 * This is network simulation, not a real cross-network measurement.
 * ------------------------------------------------------------------ */

#define PROXY_MAX_LINKS 4
#define PROXY_BUFFER 8192
#define PROXY_DELAY_MS 20

typedef struct ProxyLink {
    bool used;
    TestSocket client;
    TestSocket server;
    uint8_t c2s[PROXY_BUFFER];
    size_t c2s_length;
    uint64_t c2s_due;
    uint8_t s2c[PROXY_BUFFER];
    size_t s2c_length;
    uint64_t s2c_due;
} ProxyLink;

typedef struct Proxy {
    TestSocket listener;
    int upstream_port;
    ProxyLink links[PROXY_MAX_LINKS];
} Proxy;


static bool proxy_start(Proxy *proxy, int upstream_port)
{
    memset(proxy, 0, sizeof(*proxy));
    proxy->upstream_port = upstream_port;
    proxy->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy->listener == TEST_INVALID) {
        return false;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(proxy->listener, (struct sockaddr *)&address,
             sizeof(address)) != 0
        || listen(proxy->listener, 8) != 0) {
        test_close_socket(proxy->listener);
        proxy->listener = TEST_INVALID;
        return false;
    }
    test_nonblocking(proxy->listener);
    return true;
}

static int proxy_port(const Proxy *proxy)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    if (getsockname(proxy->listener, (struct sockaddr *)&address,
                    &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

static void proxy_link_close(ProxyLink *link)
{
    if (!link->used) {
        return;
    }
    test_close_socket(link->client);
    test_close_socket(link->server);
    memset(link, 0, sizeof(*link));
}

static void proxy_accept(Proxy *proxy)
{
    for (;;) {
        const TestSocket client = accept(proxy->listener, NULL, NULL);
        if (client == TEST_INVALID) {
            return;
        }
        ProxyLink *slot = NULL;
        for (int i = 0; i < PROXY_MAX_LINKS; ++i) {
            if (!proxy->links[i].used) {
                slot = &proxy->links[i];
                break;
            }
        }
        if (slot == NULL) {
            test_close_socket(client);
            continue;
        }
        const TestSocket upstream = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons((uint16_t)proxy->upstream_port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (upstream == TEST_INVALID
            || connect(upstream, (struct sockaddr *)&address,
                       sizeof(address)) != 0) {
            if (upstream != TEST_INVALID) {
                test_close_socket(upstream);
            }
            test_close_socket(client);
            continue;
        }
        test_nonblocking(client);
        test_nonblocking(upstream);
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->client = client;
        slot->server = upstream;
    }
}

/* Reads up to the free space in one direction and stamps the due time on
 * the first byte of a batch. Returns false on EOF/error. */
static bool proxy_read(TestSocket from, uint8_t *buffer, size_t *used,
                       uint64_t *due, uint64_t delay)
{
    const size_t free_space = PROXY_BUFFER - *used;
    if (free_space == 0) {
        return true;
    }
    const int read = (int)recv(from, (char *)buffer + *used,
                               (int)free_space, 0);
    if (read == 0) {
        return false;
    }
    if (read < 0) {
        return test_read_would_block() ? true : false;
    }
    if (*used == 0) {
        *due = test_now_ms() + delay;
    }
    *used += (size_t)read;
    return true;
}

static void proxy_write(TestSocket to, uint8_t *buffer, size_t *used,
                        uint64_t *due)
{
    if (*used == 0 || test_now_ms() < *due) {
        return;
    }
    const int written = (int)send(to, (const char *)buffer, (int)*used, 0);
    if (written > 0) {
        memmove(buffer, buffer + written, *used - (size_t)written);
        *used -= (size_t)written;
        if (*used == 0) {
            *due = 0;
        }
    }
}

static void proxy_pump(Proxy *proxy)
{
    proxy_accept(proxy);
    for (int i = 0; i < PROXY_MAX_LINKS; ++i) {
        ProxyLink *link = &proxy->links[i];
        if (!link->used) {
            continue;
        }
        const bool alive =
            proxy_read(link->client, link->c2s, &link->c2s_length,
                       &link->c2s_due, PROXY_DELAY_MS)
            && proxy_read(link->server, link->s2c, &link->s2c_length,
                          &link->s2c_due, PROXY_DELAY_MS);
        if (!alive) {
            proxy_link_close(link);
            continue;
        }
        proxy_write(link->server, link->c2s, &link->c2s_length,
                    &link->c2s_due);
        proxy_write(link->client, link->s2c, &link->s2c_length,
                    &link->s2c_due);
    }
}

static void proxy_stop(Proxy *proxy)
{
    for (int i = 0; i < PROXY_MAX_LINKS; ++i) {
        proxy_link_close(&proxy->links[i]);
    }
    if (proxy->listener != TEST_INVALID) {
        test_close_socket(proxy->listener);
        proxy->listener = TEST_INVALID;
    }
}

static Proxy *g_proxy = NULL;

static void proxy_pump_hook(void)
{
    if (g_proxy != NULL) {
        proxy_pump(g_proxy);
    }
}

static void test_latency_game(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 5000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    Proxy proxy;
    REVERSI_TEST_ASSERT(stats, proxy_start(&proxy, net_server_port(server)));
    const int proxy_port_number = proxy_port(&proxy);
    REVERSI_TEST_ASSERT(stats, proxy_port_number > 0);
    g_proxy = &proxy;
    g_pump_hook = proxy_pump_hook;
    play_verified_game(stats, server, proxy_port_number, 5000);
    g_pump_hook = NULL;
    g_proxy = NULL;
    proxy_stop(&proxy);
    net_server_destroy(server);
}


#if !defined(_WIN32)
static void test_logs_redacted(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 5000, .quiet = false};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    TestClient a;
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));

    FILE *capture = tmpfile();
    REVERSI_TEST_ASSERT(stats, capture != NULL);
    fflush(stderr);
    const int saved = dup(fileno(stderr));
    REVERSI_TEST_ASSERT(stats, saved >= 0);
    REVERSI_TEST_ASSERT(stats, dup2(fileno(capture), fileno(stderr)) >= 0);

    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    bool ok = client_expect(server, &a, NET_MSG_WELCOME, &message, 2000);
    char token[33] = "";
    snprintf(token, sizeof(token), "%s", message.token);
    ok = ok && client_expect(server, &a, NET_MSG_ROOM, &message, 2000);
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    ok = ok && client_expect(server, &b, NET_MSG_ROOM, &message, 2000);
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    ok = ok && pump_both(server, &a, &b, &start_a, &start_b, NET_MSG_START,
                         3000);
    test_send(&a, &(NetMessage){.type = NET_MSG_RESIGN});
    ok = ok && client_expect(server, &a, NET_MSG_GAMEOVER, &message, 2000);

    fflush(stderr);
    REVERSI_TEST_ASSERT(stats, dup2(saved, fileno(stderr)) >= 0);
    close(saved);
    char log[16384];
    rewind(capture);
    const size_t length = fread(log, 1, sizeof(log) - 1, capture);
    log[length] = '\0';
    fclose(capture);

    REVERSI_TEST_ASSERT(stats, ok);
    REVERSI_TEST_ASSERT(stats, length > 0);
    REVERSI_TEST_ASSERT(stats, strstr(log, "room ") != NULL);
    /* Tokens and protocol payloads must never reach the log. */
    REVERSI_TEST_ASSERT(stats, token[0] != '\0');
    REVERSI_TEST_ASSERT(stats, strstr(log, token) == NULL);
    REVERSI_TEST_ASSERT(stats, strstr(log, "board=") == NULL);
    REVERSI_TEST_ASSERT(stats, strstr(log, "token=") == NULL);

    test_close(&a);
    test_close(&b);
    net_server_destroy(server);
}
#endif


/* One complete two-client game on an already-running server, checked
 * against a local replay. Used by the latency proxy and the soak test. */
static void play_verified_game(TestStats *stats, NetServer *server, int port,
                               int timeout_ms)
{
    TestClient a;
    TestClient b;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &a, port));
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    NetMessage message;
    test_send(&a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&a, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &a, NET_MSG_ROOM,
                                             &message, timeout_ms));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &join);
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, timeout_ms));
    test_send(&a, &(NetMessage){.type = NET_MSG_READY});
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    NetMessage start_a;
    NetMessage start_b;
    REVERSI_TEST_ASSERT(stats, pump_both(server, &a, &b, &start_a, &start_b,
                                         NET_MSG_START, timeout_ms));

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
        const NetMessage move = {.type = NET_MSG_MOVE, .seq = seq,
                                 .last = moves[0], .has_last = true};
        REVERSI_TEST_ASSERT(stats, test_send(mover, &move));
        NetMessage state_a;
        NetMessage state_b;
        REVERSI_TEST_ASSERT(stats, client_expect(server, mover, NET_MSG_STATE,
                                                 &state_a, timeout_ms));
        REVERSI_TEST_ASSERT(stats, client_expect(server, other, NET_MSG_STATE,
                                                 &state_b, timeout_ms));
        REVERSI_TEST_ASSERT(stats, state_a.seq == seq + 1);
        REVERSI_TEST_ASSERT(stats, state_b.seq == seq + 1);
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
            REVERSI_TEST_ASSERT(stats, client_expect(server, &a,
                                                     NET_MSG_GAMEOVER,
                                                     &over_a, timeout_ms));
            REVERSI_TEST_ASSERT(stats, client_expect(server, &b,
                                                     NET_MSG_GAMEOVER,
                                                     &over_b, timeout_ms));
            REVERSI_TEST_ASSERT(stats, over_a.result == local.result);
            finished = true;
        }
    }
    REVERSI_TEST_ASSERT(stats, finished);
    test_close(&a);
    test_close(&b);
}

static void test_soak_games(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 2000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    /* Eight games by default (deterministic assertion count); set
     * REVERSI_SOAK_MS for a long soak, e.g. REVERSI_SOAK_MS=600000. */
    const char *requested = getenv("REVERSI_SOAK_MS");
    int duration_ms = 0;
    if (requested != NULL) {
        const int value = atoi(requested);
        if (value > 0) {
            duration_ms = value;
        }
    }
    int games = 0;
    if (duration_ms > 0) {
        const uint64_t deadline = test_now_ms() + (uint64_t)duration_ms;
        do {
            play_verified_game(stats, server, port, 3000);
            ++games;
        } while (test_now_ms() < deadline);
        REVERSI_TEST_ASSERT(stats, games >= 2);
    } else {
        while (games < 8) {
            play_verified_game(stats, server, port, 3000);
            ++games;
        }
    }
    /* The server keeps serving after the churn. */
    play_verified_game(stats, server, port, 3000);
    net_server_destroy(server);
}

#if !defined(__APPLE__)
/* Pump the fast client's socket, recording whether it saw the opponent
 * disconnect and the last state sequence. */
static bool pump_opponent_notice(NetServer *server, TestClient *client,
                                 int timeout_ms, bool *notice, int *last_seq)
{
    for (int elapsed = 0; elapsed < timeout_ms && !*notice; elapsed += 2) {
        net_server_poll(server, 1);
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client->socket, &read_set);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
        if (select((int)client->socket + 1, &read_set, NULL, NULL,
                   &timeout) > 0) {
            test_read_available(client);
        }
        NetMessage message;
        while (parse_queued(client, &message)) {
            if (message.type == NET_MSG_OPPONENT
                && strcmp(message.status, "disconnected") == 0) {
                *notice = true;
            } else if (message.type == NET_MSG_STATE) {
                *last_seq = message.seq;
            }
        }
        test_sleep_ms(1);
    }
    return *notice;
}

/* A client that stops reading must be closed once the outbound queue is
 * full, without disturbing the other player. The socket buffers are shrunk
 * through NetServerConfig so the test reaches 16 KiB-equivalent pressure in
 * a few moves.
 *
 * macOS cannot produce that pressure: it overrides the requested 1 KiB
 * SO_RCVBUF with an auto-tuned 326,640 B window, and a full 60-move game
 * only emits ~7.8 KiB of state frames, so the kernel never returns
 * EWOULDBLOCK and the queue is never used. Measured 2026-10-30: with the
 * guard removed the test ran all 60 plies and the opponent never saw a
 * disconnect. Linux and Windows honour the requested buffer and run this
 * case in CI; macOS exercises the same code path only through the other,
 * smaller-queue cases. */
static void test_send_queue_saturation(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 3000,
                              .send_queue_bytes = 1024,
                              .send_buffer_bytes = 1024, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);

    TestClient b;
    TestClient slow;
    REVERSI_TEST_ASSERT(stats, test_connect(server, &b, port));
    NetMessage message;
    test_send(&b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&b, &(NetMessage){.type = NET_MSG_CREATE, .color = STONE_BLACK});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_ROOM,
                                             &message, 2000));
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", message.code);

    /* The slow client advertises a 1 KiB receive window and then stops
     * reading after learning its seat. */
    REVERSI_TEST_ASSERT(stats, test_connect_buffer(server, &slow, port, 1024));
    /* The slow client never reads after the handshake: its receive window
     * stays small and the server's outbound queue must fill. */
    test_send(&slow, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    test_send(&slow, &join);
    test_send(&b, &(NetMessage){.type = NET_MSG_READY});
    test_send(&slow, &(NetMessage){.type = NET_MSG_READY});
    bool notice = false;
    int last_seq = 0;
    pump_opponent_notice(server, &b, 300, &notice, &last_seq);

    GameState local;
    game_init(&local);
    int seq = 0;
    for (int ply = 0; ply < 60 && !notice; ++ply) {
        TestClient *mover = local.current_player == STONE_BLACK ? &b : &slow;
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &local.board, local.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            break;
        }
        const NetMessage move = {.type = NET_MSG_MOVE, .seq = seq,
                                 .last = moves[0], .has_last = true};
        if (!test_send(mover, &move)) {
            break;
        }
        GameState next = local;
        if (!game_apply_move(&next, moves[0])) {
            break;
        }
        local = next;
        ++seq;
        pump_opponent_notice(server, &b, 100, &notice, &last_seq);
        if (local.result != GAME_RESULT_ONGOING) {
            break;
        }
    }
    REVERSI_TEST_ASSERT(stats, notice);
    REVERSI_TEST_ASSERT(stats, last_seq >= 1);

    /* The remaining player is still served. */
    test_send(&b, &(NetMessage){.type = NET_MSG_PING});
    REVERSI_TEST_ASSERT(stats, client_expect(server, &b, NET_MSG_PONG,
                                             &message, 2000));
    test_close(&b);
    test_close(&slow);
    net_server_destroy(server);
}
#endif

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
    REVERSI_RUN_TEST(&stats, test_move_timeout);
    REVERSI_RUN_TEST(&stats, test_rate_limit);
    REVERSI_RUN_TEST(&stats, test_rejoin_timeout);
    REVERSI_RUN_TEST(&stats, test_room_full);
    REVERSI_RUN_TEST(&stats, test_rejoin_finished);
    REVERSI_RUN_TEST(&stats, test_fuzz_survives);
    REVERSI_RUN_TEST(&stats, test_hello_order);
    REVERSI_RUN_TEST(&stats, test_handshake_timeout);
    REVERSI_RUN_TEST(&stats, test_idle_timeout);
    REVERSI_RUN_TEST(&stats, test_room_ttl);
    REVERSI_RUN_TEST(&stats, test_room_ttl_refresh);
    REVERSI_RUN_TEST(&stats, test_waiting_room_reconnect);
    REVERSI_RUN_TEST(&stats, test_rejoin_takeover);
    REVERSI_RUN_TEST(&stats, test_join_failure_limit);
    REVERSI_RUN_TEST(&stats, test_connection_cap);
    REVERSI_RUN_TEST(&stats, test_room_cap);
    REVERSI_RUN_TEST(&stats, test_server_shutdown);
#if !defined(__APPLE__)
    REVERSI_RUN_TEST(&stats, test_send_queue_saturation);
#endif
    REVERSI_RUN_TEST(&stats, test_latency_game);
    REVERSI_RUN_TEST(&stats, test_soak_games);
#if !defined(_WIN32)
    REVERSI_RUN_TEST(&stats, test_logs_redacted);
#endif
    return reversi_test_summary(&stats, "net.server");
}
