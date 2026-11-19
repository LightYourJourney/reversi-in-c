/*
 * Opt-in check of the real trust store against public endpoints, on every
 * platform that builds TLS (Windows included). It uses only the portable
 * client API, unlike tests/test_net_tls.c, which needs POSIX and the
 * openssl CLI to mint its own certificates.
 *
 * A valid chain reaches the WebSocket upgrade, which the web server refuses:
 * that is "handshake_failed", never a certificate error. Set
 * REVERSI_TLS_PUBLIC=1 to run it; it needs outbound network access, and a
 * host that cannot be reached is reported as skipped, not passed.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct Events {
    NetClientStatus status;
    char reason[64];
} Events;

static void on_message(void *context, const NetMessage *message)
{
    (void)context;
    (void)message;
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
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void test_public_certificates(TestStats *stats)
{
    struct {
        const char *host;
        const char *reason;   /* NULL means "chain must verify" */
    } cases[] = {
        {"example.com", NULL},
        {"expired.badssl.com", "tls_certificate_invalid"},
        {"self-signed.badssl.com", "tls_certificate_invalid"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Events events = {0};
        NetClient *client = net_client_create(on_message, on_status, &events);
        REVERSI_TEST_ASSERT(stats, client != NULL);
        if (client == NULL) {
            continue;
        }
        /* badssl.com sometimes drops the connection before sending its
         * certificate (OpenSSL: "unexpected eof"), which surfaces as
         * tls_handshake_failed. Retry that transport failure a few times, a second apart;
         * any other outcome is final. */
        for (int attempt = 0; attempt < 3; ++attempt) {
            memset(&events, 0, sizeof(events));
            REVERSI_TEST_ASSERT(stats,
                net_client_connect_tls(client, cases[i].host, 443));
            for (int elapsed = 0; elapsed < 10000
                 && events.status != NET_CLIENT_FAILED; elapsed += 10) {
                net_client_poll(client);
                sleep_ms(10);
            }
            if (strcmp(events.reason, "tls_handshake_failed") != 0) {
                break;
            }
            sleep_ms(1000);
        }
        /* Reachability failures are the environment's problem, not the
         * client's: skip instead of inventing a pass. */
        if (events.status == NET_CLIENT_FAILED
            && (strcmp(events.reason, "connect_failed") == 0
                || strcmp(events.reason, "resolve_failed") == 0)) {
            printf("  (skipped %s: %s)\n", cases[i].host, events.reason);
            net_client_destroy(client);
            continue;
        }
        const char *expected = cases[i].reason != NULL ? cases[i].reason
                                                       : "handshake_failed";
        if (strcmp(events.reason, expected) != 0) {
            fprintf(stderr, "  %s: expected %s, got status %d reason %s\n",
                    cases[i].host, expected, (int)events.status,
                    events.reason);
        }
        REVERSI_TEST_ASSERT(stats, events.status == NET_CLIENT_FAILED);
        REVERSI_TEST_ASSERT(stats, strcmp(events.reason, expected) == 0);
        net_client_destroy(client);
    }
}

int main(void)
{
    TestStats stats = {0};
    const char *enabled = getenv("REVERSI_TLS_PUBLIC");
    if (enabled == NULL || enabled[0] == '\0' || strcmp(enabled, "0") == 0) {
        printf("  (set REVERSI_TLS_PUBLIC=1 to check public CAs)\n");
        return reversi_test_summary(&stats, "net.tls_public");
    }
    REVERSI_RUN_TEST(&stats, test_public_certificates);
    return reversi_test_summary(&stats, "net.tls_public");
}
