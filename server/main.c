#include "net/net_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--bind ADDRESS] [--port PORT] [--quiet]\n"
            "  --bind  default 127.0.0.1 (never expose publicly by default)\n"
            "  --port  default 8080; 0 selects an ephemeral port\n",
            program);
}

int main(int argc, char **argv)
{
    const char *bind_address = "127.0.0.1";
    int port = 8080;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            bind_address = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "--help") == 0
                   || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (port < 0 || port > 65535) {
        fprintf(stderr, "invalid port\n");
        return 2;
    }
    signal(SIGINT, handle_signal);
#if defined(SIGTERM)
    signal(SIGTERM, handle_signal);
#endif
#if defined(_WIN32)
    signal(SIGBREAK, handle_signal);
#endif

    NetServerConfig config = {
        .bind_address = bind_address,
        .port = port,
        .move_timeout_ms = 120000,
        .reconnect_timeout_ms = 60000,
        .quiet = quiet
    };
    NetServer *server = net_server_create(&config);
    if (server == NULL) {
        fprintf(stderr, "cannot create server\n");
        return 1;
    }
    char error[128];
    if (!net_server_start(server, error, sizeof(error))) {
        fprintf(stderr, "cannot start server: %s\n", error);
        net_server_destroy(server);
        return 1;
    }
    fprintf(stderr, "Reversi server listening on ws://%s:%d\n", bind_address,
            net_server_port(server));
    fprintf(stderr, "Local only by default; terminate TLS in a reverse proxy "
                    "before exposing WSS publicly.\n");
    while (!g_stop) {
        net_server_poll(server, 50);
    }
    net_server_shutdown(server, "shutdown");
    net_server_destroy(server);
    return 0;
}
