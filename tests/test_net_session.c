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

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_session_flow);
    return reversi_test_summary(&stats, "net.session");
}
