#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"
#include "net/net_server.h"
#include "net/net_ws.h"
#include "test_harness.h"
#include "test_socket.h"

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
    net_client_send(a2, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
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

static void test_queue_overflow(TestStats *stats)
{
    NetServerConfig config = {.bind_address = "127.0.0.1", .port = 0,
                              .quiet = true};
    NetServer *server = net_server_create(&config);
    char error[128];
    REVERSI_TEST_ASSERT(stats, net_server_start(server, error, sizeof(error)));
    const int port = net_server_port(server);
    Events events = {0};
    NetClient *a = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, a != NULL);
    REVERSI_TEST_ASSERT(stats, net_client_connect(a, "127.0.0.1", port));
    REVERSI_TEST_ASSERT(stats, wait_status(server, a, &events,
                                           NET_CLIENT_CONNECTED, 3000));
    net_client_send(a, &(NetMessage){.type = NET_MSG_HELLO, .version = 1});
    /* 38 pongs against a 32-entry receive queue: the caller does not drain
     * the queue, so it must fail loudly instead of dropping state. */
    for (int i = 0; i < 38; ++i) {
        net_client_send(a, &(NetMessage){.type = NET_MSG_PING});
    }
    for (int elapsed = 0; elapsed < 1000; ++elapsed) {
        net_server_poll(server, 1);
        test_sleep_ms(1);
    }
    net_client_poll(a);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_FAILED);
    REVERSI_TEST_ASSERT(stats, strcmp(events.reason, "queue_overflow") == 0);
    net_client_destroy(a);
    net_server_destroy(server);
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

/* A listener that accepts one client, completes the WebSocket handshake and
 * then never reads. The client must give up within its bounded write instead
 * of blocking the caller (the UI thread in the real app). */
static void test_send_to_stalled_server(TestStats *stats)
{
    /* Create the client first: on Windows it performs WSAStartup, which must
     * precede any socket() call. */
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);

    int buffer_size = 1024;
    int port = 0;
    TestSocket listener = test_listen_loopback(buffer_size, &port);
    REVERSI_TEST_ASSERT(stats, listener != REVERSI_TEST_SOCKET_INVALID);
    REVERSI_TEST_ASSERT(stats, net_client_connect(client, "127.0.0.1", port));

    /* Complete the handshake, then stop reading for good. */
    char response[256];
    TestSocket server = test_accept_websocket(listener, response);
    REVERSI_TEST_ASSERT(stats, server != REVERSI_TEST_SOCKET_INVALID);
    setsockopt(server, SOL_SOCKET, SO_RCVBUF,
               (const char *)&buffer_size, sizeof(buffer_size));
    REVERSI_TEST_ASSERT(stats,
        test_send_raw(server, response, strlen(response)));

    for (int elapsed = 0; elapsed < 3000
         && events.status != NET_CLIENT_CONNECTED; ++elapsed) {
        net_client_poll(client);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_CONNECTED);

    /* The caller must never wait for the socket. A peer that stopped
     * reading fills the 16 KiB outbound queue; every send up to that point
     * returns without touching the network, and the next one is refused.
     * A tight loop is deliberate: it lets the queue, not the kernel's
     * auto-tuned receive window, decide when the client gives up. */
    const uint64_t begin = test_now_ms();
    bool refused = false;
    int sent = 0;
    uint64_t slowest = 0;
    for (int i = 0; i < 2000000 && !refused; ++i) {
        const uint64_t call_begin = test_now_ms();
        if (!net_client_send(client, &(NetMessage){.type = NET_MSG_PING})) {
            refused = true;
        }
        const uint64_t took = test_now_ms() - call_begin;
        if (took > slowest) {
            slowest = took;
        }
        ++sent;
    }
    const uint64_t elapsed_ms = test_now_ms() - begin;
    REVERSI_TEST_ASSERT(stats, refused);
    REVERSI_TEST_ASSERT(stats, sent < 2000000);
    /* The old synchronous path spent up to 1 s in a single call. The bound
     * is loose enough for a loaded CI runner (a descheduled thread can add
     * tens of ms) and still far below that. */
    REVERSI_TEST_ASSERT(stats, elapsed_ms < 3000);
    REVERSI_TEST_ASSERT(stats, slowest < 250);

    net_client_poll(client);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_DISCONNECTED);
    REVERSI_TEST_ASSERT(stats, strcmp(events.reason, "send_queue_full") == 0);
    /* Further sends fail immediately instead of waiting again. */
    REVERSI_TEST_ASSERT(stats,
        !net_client_send(client, &(NetMessage){.type = NET_MSG_PING}));

    net_client_destroy(client);
    test_close_socket(server);
    test_close_socket(listener);
}


/* A black-holed *private* address makes connect() hang on most networks.
 * Cancelling must not wait for the OS timeout. The client refuses public
 * addresses outright (see test_public_address_refused), so this has to stay
 * inside 10/8. */
static void test_connect_cancelled(TestStats *stats)
{
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats, net_client_connect(client, "10.255.255.1", 9));
    test_sleep_ms(200);
    const uint64_t begin = test_now_ms();
    net_client_disconnect(client);
    const uint64_t elapsed = test_now_ms() - begin;
    REVERSI_TEST_ASSERT(stats, elapsed < 2000);
    REVERSI_TEST_ASSERT(stats, net_client_status(client) == NET_CLIENT_IDLE);
    net_client_destroy(client);
}

/* The connect itself is bounded at 5 s; on networks that reject the address
 * immediately this checks the failure reporting instead. */
static void test_connect_timeout(TestStats *stats)
{
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    const uint64_t begin = test_now_ms();
    REVERSI_TEST_ASSERT(stats, net_client_connect(client, "10.255.255.1", 9));
    bool failed = false;
    for (int elapsed = 0; elapsed < 8000 && !failed; ++elapsed) {
        net_client_poll(client);
        if (events.status == NET_CLIENT_FAILED) {
            failed = true;
        }
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, failed);
    REVERSI_TEST_ASSERT(stats, test_now_ms() - begin < 7000);
    net_client_destroy(client);
}

/* The native client has no TLS, so a public peer is refused before any
 * socket is opened rather than downgraded to clear text. */
static void test_public_address_refused(TestStats *stats)
{
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    const uint64_t begin = test_now_ms();
    REVERSI_TEST_ASSERT(stats, net_client_connect(client, "8.8.8.8", 443));
    bool failed = false;
    for (int elapsed = 0; elapsed < 3000 && !failed; ++elapsed) {
        net_client_poll(client);
        if (events.status == NET_CLIENT_FAILED) {
            failed = true;
        }
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, failed);
    REVERSI_TEST_ASSERT(stats, strcmp(events.reason, "tls_required") == 0);
    REVERSI_TEST_ASSERT(stats, test_now_ms() - begin < 1000);
    net_client_destroy(client);
}

/* A fake server: listens, lets the client connect and reads its upgrade
 * request; the 101 response is left in response for the caller to send. */
static bool fake_server_accept(TestSocket *listener, TestSocket *server,
                               NetClient *client, char response[256])
{
    int port = 0;
    *listener = test_listen_loopback(0, &port);
    *server = REVERSI_TEST_SOCKET_INVALID;
    if (*listener == REVERSI_TEST_SOCKET_INVALID
        || !net_client_connect(client, "127.0.0.1", port)) {
        return false;
    }
    *server = test_accept_websocket(*listener, response);
    return *server != REVERSI_TEST_SOCKET_INVALID;
}

static size_t pong_frame(uint8_t *out, size_t capacity)
{
    static const char text[] = "pong";
    return net_ws_encode(0x1, (const uint8_t *)text, strlen(text), false,
                         NULL, out, capacity);
}

static bool wait_for_pong(NetClient *client, Events *events)
{
    NetMessage message;
    return wait_message(NULL, client, NULL, events, NET_MSG_PONG, &message,
                        3000);
}

static void test_frame_with_handshake_response(TestStats *stats)
{
    /* A frame in the same segment as the 101 response must be parsed, not
     * discarded with the rest of the handshake buffer. */
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    TestSocket listener = REVERSI_TEST_SOCKET_INVALID;
    TestSocket server = REVERSI_TEST_SOCKET_INVALID;
    char response[256];
    REVERSI_TEST_ASSERT(stats, fake_server_accept(&listener, &server, client,
                                                  response));
    uint8_t wire[512];
    const size_t header = strlen(response);
    memcpy(wire, response, header);
    const size_t frame = pong_frame(wire + header, sizeof(wire) - header);
    REVERSI_TEST_ASSERT(stats, frame > 0);
    REVERSI_TEST_ASSERT(stats, test_send_raw(server, wire, header + frame));
    REVERSI_TEST_ASSERT(stats, wait_for_pong(client, &events));
    net_client_destroy(client);
    test_close_socket(server);
    test_close_socket(listener);
}

static void test_frame_split_across_reads(TestStats *stats)
{
    /* A frame whose bytes arrive in two reads must be reassembled. */
    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    TestSocket listener = REVERSI_TEST_SOCKET_INVALID;
    TestSocket server = REVERSI_TEST_SOCKET_INVALID;
    char response[256];
    REVERSI_TEST_ASSERT(stats, fake_server_accept(&listener, &server, client,
                                                  response));
    REVERSI_TEST_ASSERT(stats,
        test_send_raw(server, response, strlen(response)));
    REVERSI_TEST_ASSERT(stats, wait_status(NULL, client, &events,
                                           NET_CLIENT_CONNECTED, 3000));
    uint8_t frame[64];
    const size_t length = pong_frame(frame, sizeof(frame));
    REVERSI_TEST_ASSERT(stats, length > 3);
    REVERSI_TEST_ASSERT(stats, test_send_raw(server, frame, 3));
    /* Give the I/O thread (20 ms select slices) time to read the half. */
    test_sleep_ms(50);
    REVERSI_TEST_ASSERT(stats, test_send_raw(server, frame + 3, length - 3));
    REVERSI_TEST_ASSERT(stats, wait_for_pong(client, &events));
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_CONNECTED);
    net_client_destroy(client);
    test_close_socket(server);
    test_close_socket(listener);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_client_flow);
    REVERSI_RUN_TEST(&stats, test_queue_overflow);
    REVERSI_RUN_TEST(&stats, test_send_to_stalled_server);
    REVERSI_RUN_TEST(&stats, test_connect_cancelled);
    REVERSI_RUN_TEST(&stats, test_connect_timeout);
    REVERSI_RUN_TEST(&stats, test_public_address_refused);
    REVERSI_RUN_TEST(&stats, test_frame_with_handshake_response);
    REVERSI_RUN_TEST(&stats, test_frame_split_across_reads);
    return reversi_test_summary(&stats, "net.client");
}
