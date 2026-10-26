#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"
#include "net/net_server.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct Events {
    NetMessage messages[32];
    int count;
    NetClientStatus status;
    char reason[64];
} Events;

static void on_message(void *context, const NetMessage *message)
{
    Events *events = context;
    if (events->count < (int)(sizeof(events->messages)
                              / sizeof(events->messages[0]))) {
        events->messages[events->count++] = *message;
    }
}

static void on_status(void *context, NetClientStatus status,
                      const char *reason)
{
    Events *events = context;
    events->status = status;
    if (reason != NULL) {
        snprintf(events->reason, sizeof(events->reason), "%s", reason);
    }
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

static bool take_message(Events *events, NetMessageType type,
                         NetMessage *out)
{
    for (int i = 0; i < events->count; ++i) {
        if (events->messages[i].type == type) {
            *out = events->messages[i];
            for (int j = i; j + 1 < events->count; ++j) {
                events->messages[j] = events->messages[j + 1];
            }
            --events->count;
            return true;
        }
    }
    return false;
}

static bool wait_message(NetServer *server, NetClient *a, NetClient *b,
                         Events *events, NetMessageType type, NetMessage *out,
                         int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 1) {
        net_client_poll(a);
        if (b != NULL) {
            net_client_poll(b);
        }
        net_server_poll(server, 1);
        if (take_message(events, type, out)) {
            return true;
        }
        test_sleep_ms(1);
    }
    return false;
}

static bool wait_status(NetServer *server, NetClient *client, Events *events,
                        NetClientStatus status, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 1) {
        net_client_poll(client);
        net_server_poll(server, 1);
        if (events->status == status) {
            return true;
        }
        test_sleep_ms(1);
    }
    return false;
}

static void test_client_flow(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);

    Events a_events = {0};
    Events b_events = {0};
    NetClient *a = net_client_create(on_message, on_status, &a_events);
    NetClient *b = net_client_create(on_message, on_status, &b_events);
    REVERSI_TEST_ASSERT(stats, a != NULL && b != NULL);
    REVERSI_TEST_ASSERT(stats, net_client_connect(a, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_status(server, a, &a_events,
                                           NET_CLIENT_CONNECTED, 3000));
    REVERSI_TEST_ASSERT(stats, net_client_send(a, &(NetMessage){
        .type = NET_MSG_HELLO, .version = 1}));
    REVERSI_TEST_ASSERT(stats, net_client_send(a, &(NetMessage){
        .type = NET_MSG_CREATE, .color = STONE_BLACK}));
    NetMessage message;
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &a_events,
                                            NET_MSG_WELCOME, &message, 3000));
    char token_a[33];
    snprintf(token_a, sizeof(token_a), "%s", message.token);
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &a_events,
                                            NET_MSG_ROOM, &message, 3000));
    char code[7];
    snprintf(code, sizeof(code), "%s", message.code);

    REVERSI_TEST_ASSERT(stats, net_client_connect(b, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_status(server, b, &b_events,
                                           NET_CLIENT_CONNECTED, 3000));
    net_client_send(b, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    NetMessage join = {.type = NET_MSG_JOIN, .color = STONE_WHITE};
    snprintf(join.code, sizeof(join.code), "%s", code);
    REVERSI_TEST_ASSERT(stats, net_client_send(b, &join));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &b_events,
                                            NET_MSG_WELCOME, &message, 3000));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &b_events,
                                            NET_MSG_ROOM, &message, 3000));

    net_client_send(a, &(NetMessage){.type = NET_MSG_READY});
    net_client_send(b, &(NetMessage){.type = NET_MSG_READY});
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &a_events,
                                            NET_MSG_START, &message, 3000));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &b_events,
                                            NET_MSG_START, &message, 3000));

    NetMessage move = {.type = NET_MSG_MOVE, .seq = 0,
                       .last = {5, 3}, .has_last = true};
    REVERSI_TEST_ASSERT(stats, net_client_send(a, &move));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &a_events,
                                            NET_MSG_STATE, &message, 3000));
    REVERSI_TEST_ASSERT(stats, message.seq == 1);
    REVERSI_TEST_ASSERT(stats, wait_message(server, a, b, &b_events,
                                            NET_MSG_STATE, &message, 3000));

    /* Disconnect and reconnect the host. */
    net_client_disconnect(a);
    REVERSI_TEST_ASSERT(stats, wait_message(server, b, NULL, &b_events,
                                            NET_MSG_OPPONENT, &message, 3000));
    Events again = {0};
    NetClient *a2 = net_client_create(on_message, on_status, &again);
    REVERSI_TEST_ASSERT(stats, net_client_connect(a2, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_status(server, a2, &again,
                                           NET_CLIENT_CONNECTED, 3000));
    NetMessage rejoin = {.type = NET_MSG_REJOIN};
    snprintf(rejoin.token, sizeof(rejoin.token), "%s", token_a);
    REVERSI_TEST_ASSERT(stats, net_client_send(a2, &rejoin));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a2, b, &again,
                                            NET_MSG_WELCOME, &message, 3000));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a2, b, &again,
                                            NET_MSG_ROOM, &message, 3000));
    REVERSI_TEST_ASSERT(stats, wait_message(server, a2, b, &again,
                                            NET_MSG_STATE, &message, 3000));

    net_client_destroy(a2);
    net_client_destroy(a);
    net_client_destroy(b);
    net_server_destroy(server);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_client_flow);
    return reversi_test_summary(&stats, "net.client");
}
