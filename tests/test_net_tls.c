/*
 * Native TLS (wss://) transport tests.
 *
 * An in-process TLS server speaks the same WebSocket handshake and text
 * protocol as reversi_server, so these exercise the real client end to end:
 * chain verification, host name verification, an untrusted issuer and a
 * connection dropped mid-stream.
 *
 * Certificates are generated with the `openssl` CLI into a temp directory;
 * the client trusts the generated CA through SSL_CERT_FILE, which is exactly
 * the hook a self-hosted deployment uses to pin its own CA.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "core/rules.h"
#include "net/net_client.h"
#include "net/net_session.h"
#include "net/net_ws.h"
#include "test_harness.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* SSL_CERT_FILE as it was when the process started: the earlier cases point
 * it at their own generated CAs, and the live case needs the real one. */
static char g_original_cert_file[512];

typedef struct TlsServer {
    int listen_fd;
    SSL_CTX *ctx;
    int port;
    int accepts;    /* how many connections to serve (default 1) */
    int accepted;   /* connections served so far */
    bool echo;      /* answer ping with pong */
    bool drop_after_welcome; /* first connection only */
    bool saw_welcome;
    volatile bool stopping;  /* lets the accept loop exit on shutdown */
} TlsServer;

typedef struct Events {
    NetClientStatus status;
    char reason[64];
    int messages;
    char last[256];
} Events;

static void on_message(void *context, const NetMessage *message)
{
    Events *events = context;
    ++events->messages;
    snprintf(events->last, sizeof(events->last), "%d",
             (int)message->type);
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

static void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void pump(NetClient *client, int ms)
{
    for (int elapsed = 0; elapsed < ms; elapsed += 2) {
        net_client_poll(client);
        sleep_ms(2);
    }
}

static bool run(const char *command)
{
    return system(command) == 0;
}

/* Writes a CA + leaf certificate for `san` into dir; returns the CA path. */
static bool make_certificate(const char *dir, const char *name,
                             const char *san, char *ca_out, size_t ca_size,
                             char *cert_out, size_t cert_size,
                             char *key_out, size_t key_size)
{
    snprintf(ca_out, ca_size, "%s/%s-ca.pem", dir, name);
    snprintf(cert_out, cert_size, "%s/%s-cert.pem", dir, name);
    snprintf(key_out, key_size, "%s/%s-key.pem", dir, name);
    char key_ca[512];
    char csr[512];
    snprintf(key_ca, sizeof(key_ca), "%s/%s-ca-key.pem", dir, name);
    snprintf(csr, sizeof(csr), "%s/%s.csr", dir, name);

    char command[2048];
    snprintf(command, sizeof(command),
             "openssl req -x509 -newkey rsa:2048 -nodes -days 2 "
             "-subj /CN=%s-test-ca -keyout %s -out %s >/dev/null 2>&1",
             name, key_ca, ca_out);
    if (!run(command)) {
        return false;
    }
    snprintf(command, sizeof(command),
             "openssl req -newkey rsa:2048 -nodes -subj /CN=%s "
             "-keyout %s -out %s >/dev/null 2>&1", san, key_out, csr);
    if (!run(command)) {
        return false;
    }
    /* `system()` runs /bin/sh, which has no process substitution: put the
     * SAN into a real file instead. */
    char ext[512];
    snprintf(ext, sizeof(ext), "%s/%s-ext.cnf", dir, name);
    FILE *file = fopen(ext, "w");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "subjectAltName=%s\n", san);
    fclose(file);
    snprintf(command, sizeof(command),
             "openssl x509 -req -in %s -CA %s -CAkey %s -CAcreateserial "
             "-days 2 -extfile %s -out %s >/dev/null 2>&1",
             csr, ca_out, key_ca, ext, cert_out);
    if (!run(command)) {
        return false;
    }
    return true;
}

static bool server_start(TlsServer *server, const char *cert,
                         const char *key)
{
    server->ctx = SSL_CTX_new(TLS_server_method());
    if (server->ctx == NULL) {
        return false;
    }
    if (SSL_CTX_use_certificate_file(server->ctx, cert,
                                     SSL_FILETYPE_PEM) != 1
        || SSL_CTX_use_PrivateKey_file(server->ctx, key,
                                       SSL_FILETYPE_PEM) != 1) {
        return false;
    }
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        return false;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server->listen_fd, (struct sockaddr *)&address,
             sizeof(address)) != 0
        || listen(server->listen_fd, 1) != 0) {
        return false;
    }
    socklen_t length = sizeof(address);
    if (getsockname(server->listen_fd, (struct sockaddr *)&address,
                    &length) != 0) {
        return false;
    }
    server->port = ntohs(address.sin_port);
    /* accept() must be interruptible: a test that never connects would
     * otherwise leave the thread blocked and pthread_join hanging. */
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
    setsockopt(server->listen_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));
    return true;
}

/* Stops the accept loop, joins it, then releases the listener and context. */
static void server_finish(TlsServer *server, pthread_t thread)
{
    server->stopping = true;
    pthread_join(thread, NULL);
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    if (server->ctx != NULL) {
        SSL_CTX_free(server->ctx);
        server->ctx = NULL;
    }
}

/* Accepts one connection, answers the upgrade, sends a welcome and then
 * echoes text frames until the peer leaves. */
static void send_text(SSL *ssl, uint64_t *rng, const char *text)
{
    uint8_t out[256];
    const size_t length = net_ws_encode(0x1, (const uint8_t *)text,
                                        strlen(text), false, rng, out,
                                        sizeof(out));
    if (length > 0) {
        SSL_write(ssl, out, (int)length);
    }
}

/* SSL_read/SSL_accept return -1 with WANT_READ when the socket timeout
 * expires; that is not the peer closing. Retry until a deadline. */
static uint64_t test_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int server_read(SSL *ssl, void *buffer, int length)
{
    const uint64_t deadline = test_now_ms() + 5000;
    for (;;) {
        const int read = SSL_read(ssl, buffer, length);
        if (read > 0) {
            return read;
        }
        const int error = SSL_get_error(ssl, read);
        if (error == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return read == 0 ? 0 : -1;
        }
        if (test_now_ms() >= deadline) {
            return -1;
        }
        sleep_ms(2);
    }
}

static void serve_one(TlsServer *server, int fd, bool drop)
{
    /* The listener carries a 100 ms accept timeout; Linux does not propagate
     * it, other systems might, and either way 100 ms is far too short for the
     * upgrade handshake on a loaded runner. Pin generous timeouts here. */
    struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    SSL *ssl = SSL_new(server->ctx);
    SSL_set_fd(ssl, fd);
    for (;;) {
        const int accepted = SSL_accept(ssl);
        if (accepted == 1) {
            break;
        }
        const int error = SSL_get_error(ssl, accepted);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            SSL_free(ssl);
            close(fd);
            return;
        }
    }
    char request[2048];
    size_t used = 0;
    while (used < sizeof(request) - 1) {
        const int read = server_read(ssl, request + used,
                                     (int)(sizeof(request) - 1 - used));
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
    if (!net_ws_parse_handshake(request, used, key)
        || !net_ws_build_handshake_response(key, response)) {
        SSL_free(ssl);
        close(fd);
        return;
    }
    if (SSL_write(ssl, response, (int)strlen(response)) <= 0) {
        SSL_free(ssl);
        close(fd);
        return;
    }
    NetWsParser parser;
    net_ws_parser_init(&parser);
    /* Frames are read into a buffer that keeps whatever the parser has not
     * consumed: one SSL_read can carry several frames (hello + rejoin). */
    uint8_t pending[4096];
    size_t pending_length = 0;
    uint64_t rng = 12345;
    bool welcome_sent = false;
    for (;;) {
        const int read = server_read(ssl, pending + pending_length,
                                     (int)(sizeof(pending) - pending_length));
        if (read <= 0) {
            break;
        }
        pending_length += (size_t)read;
        size_t offset = 0;
        bool dropped = false;
        while (offset < pending_length) {
            NetWsFrame frame;
            size_t consumed = 0;
            const NetWsResult result = net_ws_parse(
                &parser, pending + offset, pending_length - offset, true,
                &frame, &consumed);
            if (result == NET_WS_NEED_MORE || consumed == 0) {
                break;
            }
            offset += consumed;
            if (result != NET_WS_TEXT) {
                continue;
            }
            if (!welcome_sent) {
                welcome_sent = true;
                server->saw_welcome = true;
                send_text(ssl, &rng, "welcome v=1 "
                          "token=0123456789abcdef0123456789abcdef player=1");
                if (drop && server->accepted == 1) {
                    dropped = true;
                    break;
                }
            } else if (frame.length >= 6
                       && memcmp(frame.payload, "rejoin", 6) == 0) {
                /* No rooms here: tell the session the seat is gone so it can
                 * settle in the lobby instead of waiting for a room frame. */
                send_text(ssl, &rng, "error error=rejoin_failed");
            } else if (server->echo && frame.length >= 4
                       && memcmp(frame.payload, "ping", 4) == 0) {
                send_text(ssl, &rng, "pong");
            }
        }
        if (dropped) {
            break;
        }
        memmove(pending, pending + offset, pending_length - offset);
        pending_length -= offset;
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
}

static void *server_thread(void *argument)
{
    TlsServer *server = argument;
    const int total = server->accepts > 0 ? server->accepts : 1;
    while (!server->stopping && server->accepted < total) {
        const int fd = accept(server->listen_fd, NULL, NULL);
        if (fd < 0) {
            continue; /* receive timeout, re-check stopping */
        }
        server->accepted += 1;
        serve_one(server, fd, server->drop_after_welcome);
    }
    return NULL;
}

static bool wait_connected(NetClient *client, const Events *events, int ms)
{
    pump(client, ms);
    const bool connected = net_client_status(client) == NET_CLIENT_CONNECTED;
    if (!connected) {
        /* The reason is the only clue when a trust chain behaves differently
         * across distros. */
        fprintf(stderr, "[tls] connect failed: status=%d reason=%s\n",
                (int)events->status, events->reason);
    }
    return connected;
}

static void test_valid_certificate(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "valid", "DNS:localhost,IP:127.0.0.1", ca, sizeof(ca), cert,
        sizeof(cert), key, sizeof(key)));
    setenv("SSL_CERT_FILE", ca, 1);

    TlsServer server = {.listen_fd = -1, .echo = true};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_client_connect_tls(client, "localhost", server.port));
    REVERSI_TEST_ASSERT(stats, wait_connected(client, &events, 5000));
    /* The server answers the first text frame with a welcome. */
    const NetMessage hello = {.type = NET_MSG_HELLO, .version = 1};
    REVERSI_TEST_ASSERT(stats, net_client_send(client, &hello));
    pump(client, 800);
    REVERSI_TEST_ASSERT(stats, events.messages >= 1);

    /* Round trip through the encrypted channel. */
    const NetMessage ping = {.type = NET_MSG_PING};
    REVERSI_TEST_ASSERT(stats, net_client_send(client, &ping));
    pump(client, 300);
    REVERSI_TEST_ASSERT(stats, events.messages >= 2);

    net_client_destroy(client);
    server_finish(&server, thread);
}

static void test_untrusted_certificate(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    char other_ca[512];
    char other_cert[512];
    char other_key[512];
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "untrusted", "DNS:localhost,IP:127.0.0.1", ca, sizeof(ca),
        cert, sizeof(cert), key, sizeof(key)));
    /* The client trusts a different CA, so this chain must be rejected. */
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "other", "DNS:localhost", other_ca, sizeof(other_ca),
        other_cert, sizeof(other_cert), other_key, sizeof(other_key)));
    setenv("SSL_CERT_FILE", other_ca, 1);

    TlsServer server = {.listen_fd = -1};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_client_connect_tls(client, "localhost", server.port));
    pump(client, 3000);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_FAILED);
    REVERSI_TEST_ASSERT(stats,
        strcmp(events.reason, "tls_certificate_invalid") == 0);
    net_client_destroy(client);
    server_finish(&server, thread);
}

static void test_hostname_mismatch(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    /* The certificate names localhost only; the client dials 127.0.0.1. */
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "hostname", "DNS:localhost", ca, sizeof(ca), cert,
        sizeof(cert), key, sizeof(key)));
    setenv("SSL_CERT_FILE", ca, 1);

    TlsServer server = {.listen_fd = -1};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_client_connect_tls(client, "127.0.0.1", server.port));
    pump(client, 3000);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_FAILED);
    REVERSI_TEST_ASSERT(stats,
        strcmp(events.reason, "tls_certificate_invalid") == 0);
    net_client_destroy(client);
    server_finish(&server, thread);
}

/* The lobby defaults to 127.0.0.1; an IP literal must be checked against the
 * certificate's IP SAN (SSL_set1_host would reject it outright). */
static void test_ip_literal_host(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "literal", "DNS:localhost,IP:127.0.0.1", ca, sizeof(ca), cert,
        sizeof(cert), key, sizeof(key)));
    setenv("SSL_CERT_FILE", ca, 1);

    TlsServer server = {.listen_fd = -1};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_client_connect_tls(client, "127.0.0.1", server.port));
    REVERSI_TEST_ASSERT(stats, wait_connected(client, &events, 5000));
    const NetMessage hello = {.type = NET_MSG_HELLO, .version = 1};
    REVERSI_TEST_ASSERT(stats, net_client_send(client, &hello));
    pump(client, 800);
    REVERSI_TEST_ASSERT(stats, events.messages >= 1);
    net_client_destroy(client);
    server_finish(&server, thread);
}

/* A TLS session must reconnect over TLS after the peer disappears; the
 * second connection is served by the same listener. */
static void test_session_reconnects_over_tls(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "reconnect", "DNS:localhost,IP:127.0.0.1", ca, sizeof(ca), cert,
        sizeof(cert), key, sizeof(key)));
    setenv("SSL_CERT_FILE", ca, 1);

    TlsServer server = {.listen_fd = -1, .accepts = 2,
                        .drop_after_welcome = true};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    NetSession *session = net_session_create();
    REVERSI_TEST_ASSERT(stats, session != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_session_connect_tls(session, "localhost", server.port));
    for (int elapsed = 0; elapsed < 5000 && session->phase != NET_SESSION_LOBBY;
         elapsed += 5) {
        net_session_update(session, 0.01f);
        sleep_ms(5);
    }
    REVERSI_TEST_ASSERT(stats, session->phase == NET_SESSION_LOBBY);

    /* The first connection is closed right after its welcome. */
    for (int elapsed = 0; elapsed < 5000
         && session->phase != NET_SESSION_DISCONNECTED; elapsed += 5) {
        net_session_update(session, 0.01f);
        sleep_ms(5);
    }
    REVERSI_TEST_ASSERT(stats, session->phase == NET_SESSION_DISCONNECTED);

    /* The session retries every 2 s and must come back over TLS. */
    for (int elapsed = 0; elapsed < 8000 && session->phase != NET_SESSION_LOBBY;
         elapsed += 5) {
        net_session_update(session, 2.1f);
        sleep_ms(5);
    }
    REVERSI_TEST_ASSERT(stats, session->phase == NET_SESSION_LOBBY);
    REVERSI_TEST_ASSERT(stats, session->use_tls);
    REVERSI_TEST_ASSERT(stats, server.accepted == 2);

    net_session_destroy(session);
    server_finish(&server, thread);
}

static void test_dropped_after_handshake(TestStats *stats, const char *dir)
{
    char ca[512];
    char cert[512];
    char key[512];
    REVERSI_TEST_ASSERT(stats, make_certificate(
        dir, "drop", "DNS:localhost,IP:127.0.0.1", ca, sizeof(ca), cert,
        sizeof(cert), key, sizeof(key)));
    setenv("SSL_CERT_FILE", ca, 1);

    TlsServer server = {.listen_fd = -1, .drop_after_welcome = true};
    REVERSI_TEST_ASSERT(stats, server_start(&server, cert, key));
    pthread_t thread;
    REVERSI_TEST_ASSERT(stats,
        pthread_create(&thread, NULL, server_thread, &server) == 0);

    Events events = {0};
    NetClient *client = net_client_create(on_message, on_status, &events);
    REVERSI_TEST_ASSERT(stats, client != NULL);
    REVERSI_TEST_ASSERT(stats,
        net_client_connect_tls(client, "localhost", server.port));
    REVERSI_TEST_ASSERT(stats, wait_connected(client, &events, 5000));
    const NetMessage hello = {.type = NET_MSG_HELLO, .version = 1};
    REVERSI_TEST_ASSERT(stats, net_client_send(client, &hello));
    pump(client, 2000);
    REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_DISCONNECTED);
    net_client_destroy(client);
    server_finish(&server, thread);
}

/* Opt-in: two TLS sessions play a full round against a real reversi_server
 * behind a TLS terminator. Set REVERSI_TLS_E2E=host:port and SSL_CERT_FILE to
 * the CA that signed the endpoint's certificate. */
static void test_live_server(TestStats *stats)
{
    if (g_original_cert_file[0] != '\0') {
        setenv("SSL_CERT_FILE", g_original_cert_file, 1);
    }
    const char *target = getenv("REVERSI_TLS_E2E");
    if (target == NULL || target[0] == '\0') {
        printf("  (set REVERSI_TLS_E2E=host:port to run the live case)\n");
        return;
    }
    char host[128];
    size_t length = strcspn(target, ":");
    if (length == 0 || length >= sizeof(host)) {
        REVERSI_TEST_ASSERT(stats, false);
        return;
    }
    memcpy(host, target, length);
    host[length] = '\0';
    const int port = atoi(target + length + 1);
    REVERSI_TEST_ASSERT(stats, port > 0);

    NetSession *a = net_session_create();
    NetSession *b = net_session_create();
    REVERSI_TEST_ASSERT(stats, a != NULL && b != NULL);
    REVERSI_TEST_ASSERT(stats, net_session_connect_tls(a, host, port));
    REVERSI_TEST_ASSERT(stats, net_session_connect_tls(b, host, port));

    const int step = 5;
    for (int elapsed = 0; elapsed < 5000; elapsed += step) {
        net_session_update(a, 0.01f);
        net_session_update(b, 0.01f);
        sleep_ms(step);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_LOBBY);
    REVERSI_TEST_ASSERT(stats, b->phase == NET_SESSION_LOBBY);

    REVERSI_TEST_ASSERT(stats, net_session_create_room(a, STONE_BLACK));
    for (int elapsed = 0; elapsed < 5000 && a->phase != NET_SESSION_WAITING;
         elapsed += step) {
        net_session_update(a, 0.01f);
        net_session_update(b, 0.01f);
        sleep_ms(step);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_WAITING);
    REVERSI_TEST_ASSERT(stats, strlen(a->code) == REVERSI_NET_CODE_LEN);

    REVERSI_TEST_ASSERT(stats, net_session_join_room(b, a->code, STONE_WHITE));
    for (int elapsed = 0; elapsed < 5000 && b->phase != NET_SESSION_WAITING;
         elapsed += step) {
        net_session_update(a, 0.01f);
        net_session_update(b, 0.01f);
        sleep_ms(step);
    }
    REVERSI_TEST_ASSERT(stats, b->phase == NET_SESSION_WAITING);

    REVERSI_TEST_ASSERT(stats, net_session_ready(a));
    REVERSI_TEST_ASSERT(stats, net_session_ready(b));
    for (int elapsed = 0; elapsed < 5000
         && (a->phase != NET_SESSION_PLAYING
             || b->phase != NET_SESSION_PLAYING); elapsed += step) {
        net_session_update(a, 0.01f);
        net_session_update(b, 0.01f);
        sleep_ms(step);
    }
    REVERSI_TEST_ASSERT(stats, a->phase == NET_SESSION_PLAYING);
    REVERSI_TEST_ASSERT(stats, b->phase == NET_SESSION_PLAYING);

    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &a->game.board, a->game.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    REVERSI_TEST_ASSERT(stats, count > 0);
    REVERSI_TEST_ASSERT(stats, net_session_move(a, moves[0]));
    for (int elapsed = 0; elapsed < 5000 && (a->seq < 1 || b->seq < 1);
         elapsed += step) {
        net_session_update(a, 0.01f);
        net_session_update(b, 0.01f);
        sleep_ms(step);
    }
    REVERSI_TEST_ASSERT(stats, a->seq == 1);
    REVERSI_TEST_ASSERT(stats, b->seq == 1);

    net_session_destroy(a);
    net_session_destroy(b);
}

int main(void)
{
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/reversi-tls-%ld", (long)getpid());
    (void)mkdir(dir, 0700);
    if (!run("command -v openssl >/dev/null 2>&1")) {
        fprintf(stderr, "openssl CLI not found; cannot generate test certs\n");
        return 1;
    }
    const char *cert_file = getenv("SSL_CERT_FILE");
    if (cert_file != NULL) {
        snprintf(g_original_cert_file, sizeof(g_original_cert_file), "%s",
                 cert_file);
    }
    TestStats stats = {0};
    printf("RUN  test_valid_certificate\n");
    test_valid_certificate(&stats, dir);
    printf("RUN  test_untrusted_certificate\n");
    test_untrusted_certificate(&stats, dir);
    printf("RUN  test_hostname_mismatch\n");
    test_hostname_mismatch(&stats, dir);
    printf("RUN  test_ip_literal_host\n");
    test_ip_literal_host(&stats, dir);
    printf("RUN  test_dropped_after_handshake\n");
    test_dropped_after_handshake(&stats, dir);
    printf("RUN  test_session_reconnects_over_tls\n");
    test_session_reconnects_over_tls(&stats, dir);
    printf("RUN  test_tls_capability\n");
    REVERSI_TEST_ASSERT(&stats, net_client_tls_selectable());
    printf("RUN  test_live_server\n");
    test_live_server(&stats);
    return reversi_test_summary(&stats, "net.tls");
}
