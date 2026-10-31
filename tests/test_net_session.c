#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "core/rules.h"
#include "net/net_server.h"
#include "net/net_session.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

static bool wait_phase(NetServer *server, NetSession *a, NetSession *b,
                       NetSessionPhase phase, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        net_session_update(a, 0.002f);
        if (b != NULL) {
            net_session_update(b, 0.002f);
        }
        net_server_poll(server, 1);
        if (a->phase == phase) {
            return true;
        }
        test_sleep_ms(1);
    }
    return false;
}

static void test_session_flow(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    NetSession *a = net_session_create();
    NetSession *b = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL && b != NULL);

    REVERSI_TEST_ASSERT(stats, net_session_connect(a, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                           NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_create_room(a, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                           NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, a->code[0] != '\0');
    REVERSI_TEST_ASSERT(stats, net_session_connect(b, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                           NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_join_room(b, a->code,
                                                      STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                           NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, a->color == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, b->color == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, net_session_ready(a));
    REVERSI_TEST_ASSERT(stats, net_session_ready(b));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, b,
                                           NET_SESSION_PLAYING, 3000));
    /* Sends are queued now, so the peer's broadcast can land an iteration
     * later; wait for it instead of sampling the same instant. */
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                           NET_SESSION_PLAYING, 3000));
    REVERSI_TEST_ASSERT(stats, b->phase == NET_SESSION_PLAYING);
    REVERSI_TEST_ASSERT(stats, net_session_can_move(a));
    REVERSI_TEST_ASSERT(stats, !net_session_can_move(b));

    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &a->game.board, a->game.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, count > 0);
    REVERSI_TEST_ASSERT(stats, net_session_move(a, moves[0]));
    for (int elapsed = 0; elapsed < 3000 && (a->seq < 1 || b->seq < 1);
         ++elapsed) {
        net_session_update(a, 0.002f);
        net_session_update(b, 0.002f);
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->seq == 1);
    REVERSI_TEST_ASSERT(stats, b->seq == 1);
    REVERSI_TEST_ASSERT(stats, b->game.current_player == b->color);

    REVERSI_TEST_ASSERT(stats, net_session_resign(a));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, b,
                                           NET_SESSION_FINISHED, 3000));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                           NET_SESSION_FINISHED, 3000));
    REVERSI_TEST_ASSERT(stats, a->game.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, b->game.result == GAME_RESULT_WHITE_WIN);

    /* The opponent leaves after the game: the result and its reason stay. */
    net_session_leave(b);
    for (int elapsed = 0; elapsed < 3000 && a->code[0] != '\0'; ++elapsed) {
        net_session_update(a, 0.002f);
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->code[0] == '\0');
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_FINISHED);
    REVERSI_TEST_ASSERT(stats, a->game.result == GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, strcmp(a->reason, "resign") == 0);
    REVERSI_TEST_ASSERT(stats, !a->active);

    /* Losing the server must not discard the retained result either. */
    net_server_destroy(server);
    for (int elapsed = 0; elapsed < 2000
         && net_client_status(a->client) != NET_CLIENT_DISCONNECTED;
         ++elapsed) {
        net_session_update(a, 0.002f);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats,
        net_client_status(a->client) == NET_CLIENT_DISCONNECTED);
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_FINISHED);
    REVERSI_TEST_ASSERT(stats, strcmp(a->reason, "resign") == 0);

    net_session_destroy(a);
    net_session_destroy(b);
}

static void test_session_server_restart(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 3000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    NetSession *a = net_session_create();
    NetSession *b = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL && b != NULL);
    REVERSI_TEST_ASSERT(stats, net_session_connect(a, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_create_room(a, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_connect(b, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                          NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_join_room(b, a->code,
                                                     STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                          NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_ready(a));
    REVERSI_TEST_ASSERT(stats, net_session_ready(b));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, b,
                                          NET_SESSION_PLAYING, 3000));
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &a->game.board, a->game.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, count > 0);
    REVERSI_TEST_ASSERT(stats, net_session_move(a, moves[0]));
    for (int elapsed = 0; elapsed < 3000 && a->seq < 1; ++elapsed) {
        net_session_update(a, 0.002f);
        net_session_update(b, 0.002f);
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->seq == 1);

    /* An abrupt server loss must be reported, never turned into an illegal
     * local game or a crash. The board is retained until the server says
     * otherwise; the session attempts a rejoin for its retry budget. */
    net_server_destroy(server);
    bool disconnected = false;
    for (int elapsed = 0; elapsed < 3000 && !disconnected; ++elapsed) {
        net_session_update(a, 0.002f);
        if (a->phase == NET_SESSION_DISCONNECTED) {
            disconnected = true;
        }
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, disconnected);
    REVERSI_TEST_ASSERT(stats, a->seq == 1);
    REVERSI_TEST_ASSERT(stats, a->game.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, a->code[0] != '\0');

    /* Restart on the same port: the client reconnects, the room is gone,
     * and the session must land back in the lobby without the old code. */
    NetServerConfig restart = config;
    restart.port = port;
    NetServer *again = net_server_create(&restart);
    REVERSI_TEST_ASSERT(stats, again != NULL);
    REVERSI_TEST_ASSERT(stats, net_server_start(again, error,
                                                sizeof(error)));
    for (int elapsed = 0; elapsed < 5000
         && a->phase != NET_SESSION_LOBBY; ++elapsed) {
        net_session_update(a, 2.1f); /* expire the 2 s reconnect timer */
        net_server_poll(again, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_LOBBY);
    REVERSI_TEST_ASSERT(stats, !a->active);
    REVERSI_TEST_ASSERT(stats, a->code[0] == '\0');
    REVERSI_TEST_ASSERT(stats, a->token[0] == '\0');
    net_server_destroy(again);
    net_session_destroy(a);
    net_session_destroy(b);
}

static void test_session_reconnect_budget(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    NetSession *a = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL);
    REVERSI_TEST_ASSERT(stats, net_session_connect(a, "127.0.0.1",
                                                   net_server_port(server)));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_create_room(a, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_WAITING, 3000));
    net_server_destroy(server);
    /* Five reconnect attempts with no server: the session must stop and
     * report FAILED instead of retrying forever. */
    for (int elapsed = 0; elapsed < 10000
         && a->phase != NET_SESSION_FAILED; ++elapsed) {
        net_session_update(a, 2.1f);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_FAILED);
    REVERSI_TEST_ASSERT(stats, a->reconnect_attempts == 5);
    net_session_destroy(a);
}


/* A public address is refused by the transport (no TLS) and the lobby must
 * explain why rather than showing a generic failure. */
static void test_session_plaintext_refused(TestStats *stats)
{
    NetSession *a = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL);
    REVERSI_TEST_ASSERT(stats, net_session_connect(a, "8.8.8.8", 443));
    for (int elapsed = 0; elapsed < 3000 && a->phase != NET_SESSION_FAILED;
         ++elapsed) {
        net_session_update(a, 0.01f);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_FAILED);
    /* The wording depends on whether this build can offer wss; both name
     * TLS as the thing that is missing. */
    REVERSI_TEST_ASSERT(stats, strstr(a->error, "TLS") != NULL);
    net_session_destroy(a);
}

static void test_session_move_log(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .move_timeout_ms = 30000,
                              .reconnect_timeout_ms = 5000, .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    NetSession *a = net_session_create();
    NetSession *b = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL && b != NULL);
    REVERSI_TEST_ASSERT(stats, net_session_connect(a, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_create_room(a, STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, NULL,
                                          NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_connect(b, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                          NET_SESSION_LOBBY, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_join_room(b, a->code,
                                                     STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, b, a,
                                          NET_SESSION_WAITING, 3000));
    REVERSI_TEST_ASSERT(stats, net_session_ready(a));
    REVERSI_TEST_ASSERT(stats, net_session_ready(b));
    REVERSI_TEST_ASSERT(stats, wait_phase(server, a, b,
                                          NET_SESSION_PLAYING, 3000));

    Move legal[REVERSI_BOARD_CELL_COUNT];
    int count = rules_generate_legal_moves(
        &a->game.board, a->game.current_player, legal,
        REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, count > 0);
    const Move first = legal[0];
    REVERSI_TEST_ASSERT(stats, net_session_move(a, first));
    for (int elapsed = 0; elapsed < 3000 && (a->seq < 1 || b->seq < 1);
         ++elapsed) {
        net_session_update(a, 0.002f);
        net_session_update(b, 0.002f);
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->moves_count == 1);
    REVERSI_TEST_ASSERT(stats, a->moves[0].row == first.row
                        && a->moves[0].col == first.col);
    REVERSI_TEST_ASSERT(stats, a->moves_complete);
    REVERSI_TEST_ASSERT(stats, b->moves_count == 1);

    count = rules_generate_legal_moves(&b->game.board,
                                       b->game.current_player, legal,
                                       REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, count > 0);
    const Move second = legal[0];
    REVERSI_TEST_ASSERT(stats, net_session_move(b, second));
    for (int elapsed = 0; elapsed < 3000 && (a->seq < 2 || b->seq < 2);
         ++elapsed) {
        net_session_update(a, 0.002f);
        net_session_update(b, 0.002f);
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, a->moves_count == 2);
    REVERSI_TEST_ASSERT(stats, a->moves[1].row == second.row
                        && a->moves[1].col == second.col);
    REVERSI_TEST_ASSERT(stats, b->moves_count == 2);
    REVERSI_TEST_ASSERT(stats, b->moves_complete);

    net_session_destroy(a);
    net_session_destroy(b);
    net_server_destroy(server);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_session_flow);
    REVERSI_RUN_TEST(&stats, test_session_server_restart);
    REVERSI_RUN_TEST(&stats, test_session_reconnect_budget);
    REVERSI_RUN_TEST(&stats, test_session_plaintext_refused);
    REVERSI_RUN_TEST(&stats, test_session_move_log);
    return reversi_test_summary(&stats, "net.session");
}
