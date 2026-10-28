#ifndef REVERSI_NET_NET_SERVER_H
#define REVERSI_NET_NET_SERVER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct NetServer NetServer;

typedef struct NetServerConfig {
    const char *bind_address;
    int port;
    int move_timeout_ms;
    int reconnect_timeout_ms;
    /* Zero selects the documented defaults (5 s / 120 s / 10 min / 2 min). */
    int handshake_timeout_ms;
    int idle_timeout_ms;
    int room_wait_ms;
    int room_done_ms;
    bool quiet;
} NetServerConfig;

NetServer *net_server_create(const NetServerConfig *config);
void net_server_destroy(NetServer *server);

/* Binds and listens; port 0 selects an ephemeral port. */
bool net_server_start(NetServer *server, char *error, size_t error_size);

/* Port actually bound (useful when config.port == 0). */
int net_server_port(const NetServer *server);

/* Processes one ready event batch; timeout_ms may be 0 for non-blocking. */
void net_server_poll(NetServer *server, int timeout_ms);

/* Tells every open room the server is going away ("shutdown") and flushes
 * the notification to clients; call before net_server_destroy on a planned
 * stop so players are not left waiting on a dead socket. */
void net_server_shutdown(NetServer *server);

#endif
