#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"
#include "net/net_server.h"
#include "net/net_ws.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET TestSocket;
#define TEST_SOCKET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int TestSocket;
#define TEST_SOCKET_INVALID (-1)
#endif

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

static void close_test_socket(TestSocket socket_fd)
{
#if defined(_WIN32)
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

/* A listener that accepts one client, completes the WebSocket handshake and
 * then never reads. The client must give up within its bounded write instead
 * of blocking the caller (the UI thread in the real app). */
static void test_send_to_stalled_server(TestStats *stats)
{
    TestSocket listener = socket(AF_INET, SOCK_STREAM, 0);
    REVERSI_TEST_ASSERT(stats, listener != TEST_SOCKET_INVALID);
    int buffer_size = 1024;
    setsockopt(listener, SOL_SOCKET, SO_RCVBUF,
               (const char *)&buffer_size, sizeof(buffer_size));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    REVERSI_TEST_ASSERT(stats, bind(listener, (struct sockaddr *)&address,
                                    sizeof(address)) == 0);
    REVERSI_TEST_ASSERT(stats, listen(listener, 1) == 0);
#if defined(_WIN32)
    int address_length = (int)sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    REVERSI_TEST_ASSERT(stats,
        getsockname(listener, (struct sockaddr *)&address,
                    &address_length) == 0);
    const int port = ntohs(address.sin_port);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats, net_client_connect(client, "127.0.0.1", port));

    TestSocket server = accept(listener, NULL, NULL);
    REVERSI_TEST_ASSERT(stats, server != TEST_SOCKET_INVALID);
    setsockopt(server, SOL_SOCKET, SO_RCVBUF,
               (const char *)&buffer_size, sizeof(buffer_size));

    /* Complete the handshake, then stop reading for good. */
    char request[2048];
    size_t used = 0;
    while (used < sizeof(request) - 1) {
        const int read = (int)recv(server, request + used,
                                   (int)(sizeof(request) - 1 - used), 0);
        if (read <= 0) {
            break;
        }
        used += (size_t)read;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) {
            break;
        }
    }
    char key[25];
    char response[256];
    REVERSI_TEST_ASSERT(stats, net_ws_parse_handshake(request, used, key));
    REVERSI_TEST_ASSERT(stats, net_ws_build_handshake_response(key, response));
    REVERSI_TEST_ASSERT(stats,
        send(server, response, (int)strlen(response), 0)
        == (int)strlen(response));

    for (int elapsed = 0; elapsed < 3000
         && events.status != NET_CLIENT_CONNECTED; ++elapsed) {
        net_client_poll(client);
        test_sleep_ms(1);
    }
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_CONNECTED);

    /* Fill both socket buffers; the write must fail within its 1 s budget
     * rather than blocking forever. */
    const uint64_t begin = test_now_ms();
    bool failed = false;
    int sent = 0;
    for (int i = 0; i < 2000000 && !failed; ++i) {
        if (!net_client_send(client, &(NetMessage){.type = NET_MSG_PING})) {
            failed = true;
        }
        ++sent;
    }
    const uint64_t elapsed_ms = test_now_ms() - begin;
    REVERSI_TEST_ASSERT(stats, failed);
    REVERSI_TEST_ASSERT(stats, sent < 2000000);
    REVERSI_TEST_ASSERT(stats, elapsed_ms < 5000);

    net_client_poll(client);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_DISCONNECTED);
    /* The send path reports "send_failed"; the worker may observe the same
     * shutdown as EOF first and report "closed". Either reason is correct. */
    REVERSI_TEST_ASSERT(stats,
        strcmp(events.reason, "send_failed") == 0
        || strcmp(events.reason, "closed") == 0);
    /* Further sends fail immediately instead of waiting again. */
    REVERSI_TEST_ASSERT(stats,
        !net_client_send(client, &(NetMessage){.type = NET_MSG_PING}));

    net_client_destroy(client);
    close_test_socket(server);
    close_test_socket(listener);
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_client_flow);
    REVERSI_RUN_TEST(&stats, test_queue_overflow);
    REVERSI_RUN_TEST(&stats, test_send_to_stalled_server);
    return reversi_test_summary(&stats, "net.client");
}
