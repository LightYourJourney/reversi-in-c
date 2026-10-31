#ifndef REVERSI_NET_NET_CLIENT_H
#define REVERSI_NET_NET_CLIENT_H

#include "net/net_protocol.h"

#include <stdbool.h>

typedef enum NetClientStatus {
    NET_CLIENT_IDLE = 0,
    NET_CLIENT_CONNECTING,
    NET_CLIENT_CONNECTED,
    NET_CLIENT_DISCONNECTED,
    NET_CLIENT_FAILED
} NetClientStatus;

typedef void (*NetClientMessageFn)(void *context, const NetMessage *message);
typedef void (*NetClientStatusFn)(void *context, NetClientStatus status,
                                  const char *reason);

/*
 * One client connection. All callbacks run on the thread that calls
 * net_client_poll(); the platform transport may use a worker thread or a
 * browser WebSocket internally. Messages are decoded into NetMessage.
 */
typedef struct NetClient NetClient;

NetClient *net_client_create(NetClientMessageFn on_message,
                             NetClientStatusFn on_status, void *context);
void net_client_destroy(NetClient *client);

/* Plain ws://. Non-local addresses are refused (reason "tls_required") so the
 * protocol is never sent in clear text to a public peer. */
bool net_client_connect(NetClient *client, const char *host, int port);

/* wss://: TLS 1.2+, chain verified against the system store (SSL_CERT_FILE is
 * honoured) and the host name verified against `host`. A build without
 * OpenSSL fails with "tls_unavailable" instead of falling back to plaintext. */
bool net_client_connect_tls(NetClient *client, const char *host, int port);

/* Whether this build exposes a *selectable* native TLS transport. The Web
 * build returns false: the browser upgrades https:// to wss:// by itself, so
 * there is nothing for the player to choose. */
bool net_client_tls_selectable(void);
void net_client_disconnect(NetClient *client);
void net_client_poll(NetClient *client);

/* Queues one message and returns immediately: the caller never waits for the
 * socket, and frames go out in call order. Returns false when the client is
 * not connected or when the 16 KiB outbound queue is full; a full queue also
 * drops the connection (reason "send_queue_full") so the session can reconnect
 * and resync instead of drifting behind. Partial writes are kept in the
 * queue and finished by the next poll. */
bool net_client_send(NetClient *client, const NetMessage *message);
NetClientStatus net_client_status(const NetClient *client);

#endif
