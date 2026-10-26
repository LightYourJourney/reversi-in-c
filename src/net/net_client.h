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

bool net_client_connect(NetClient *client, const char *host, int port);
void net_client_disconnect(NetClient *client);
void net_client_poll(NetClient *client);
bool net_client_send(NetClient *client, const NetMessage *message);
NetClientStatus net_client_status(const NetClient *client);

#endif
