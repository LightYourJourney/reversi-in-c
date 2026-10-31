/*
 * Browser transport for the online client. The browser owns the WebSocket;
 * this file only queues text messages for net_client_poll() and forwards
 * sends. All callbacks run on the main thread.
 */
#include "net/net_client.h"

#include <emscripten.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The EM_JS callbacks pass Connect/Disconnected/Failed as 2/3/4. */
_Static_assert(NET_CLIENT_CONNECTING == 1, "web status ordinals changed");
_Static_assert(NET_CLIENT_CONNECTED == 2, "web status ordinals changed");
_Static_assert(NET_CLIENT_DISCONNECTED == 3, "web status ordinals changed");
_Static_assert(NET_CLIENT_FAILED == 4, "web status ordinals changed");

#define REVERSI_NET_WEB_QUEUE 32

typedef struct WebQueueEntry {
    char text[REVERSI_NET_MAX_MESSAGE + 1];
} WebQueueEntry;

struct NetClient {
    NetClientMessageFn on_message;
    NetClientStatusFn on_status;
    void *context;
    NetClientStatus status;
    NetClientStatus reported;
    char reason[64];
    WebQueueEntry queue[REVERSI_NET_WEB_QUEUE];
    int head;
    int count;
};

static NetClient *g_web_client = NULL;

void reversi_net_client_web_message(const char *text);
void reversi_net_client_web_status(int status, const char *reason);

EM_JS(void, reversi_web_net_connect, (const char *host, int port), {
    var scheme = globalThis.location
        && globalThis.location.protocol === 'https:' ? 'wss://' : 'ws://';
    var socket = new WebSocket(scheme + UTF8ToString(host) + ':' + port + '/');
    globalThis.__reversiNetSocket = socket;
    socket.onopen = function () {
        Module.ccall('reversi_net_client_web_status', null,
            ['number', 'string'], [2, 'connected']);
    };
    socket.onmessage = function (event) {
        Module.ccall('reversi_net_client_web_message', null,
            ['string'], [String(event.data)]);
    };
    socket.onerror = function () {
        Module.ccall('reversi_net_client_web_status', null,
            ['number', 'string'], [4, 'socket_error']);
    };
    socket.onclose = function () {
        globalThis.__reversiNetSocket = null;
        Module.ccall('reversi_net_client_web_status', null,
            ['number', 'string'], [3, 'closed']);
    };
})

EM_JS(int, reversi_web_net_send, (const char *text), {
    var socket = globalThis.__reversiNetSocket;
    if (socket && socket.readyState === 1) {
        try {
            // Bound the browser's outbound buffer: a peer that stops reading
            // must not grow memory without limit. 64 KiB is hundreds of
            // times the largest protocol message.
            if (socket.bufferedAmount > 65536) {
                return 0;
            }
            socket.send(UTF8ToString(text));
            return 1;
        } catch (error) {
            return 0;
        }
    }
    return 0;
})

EM_JS(void, reversi_web_net_close, (void), {
    var socket = globalThis.__reversiNetSocket;
    if (socket) {
        socket.onclose = null;
        socket.close();
        globalThis.__reversiNetSocket = null;
    }
})

static void web_set_status(NetClient *client, NetClientStatus status,
                           const char *reason)
{
    client->status = status;
    if (reason != NULL) {
        snprintf(client->reason, sizeof(client->reason), "%s", reason);
    }
}

EMSCRIPTEN_KEEPALIVE
void reversi_net_client_web_message(const char *text)
{
    NetClient *client = g_web_client;
    if (client == NULL || text == NULL) {
        return;
    }
    const size_t length = strlen(text);
    if (length > REVERSI_NET_MAX_MESSAGE) {
        return;
    }
    if (client->count >= REVERSI_NET_WEB_QUEUE) {
        /* Never drop a server message silently: the state would silently
         * diverge from the server. Close the socket and let the session
         * reconnect and resync from the authoritative snapshot. */
        reversi_web_net_close();
        client->head = 0;
        client->count = 0;
        web_set_status(client, NET_CLIENT_FAILED, "queue_overflow");
        return;
    }
    const int index = (client->head + client->count) % REVERSI_NET_WEB_QUEUE;
    memcpy(client->queue[index].text, text, length);
    client->queue[index].text[length] = '\0';
    ++client->count;
}

EMSCRIPTEN_KEEPALIVE
void reversi_net_client_web_status(int status, const char *reason)
{
    if (g_web_client == NULL) {
        return;
    }
    web_set_status(g_web_client, (NetClientStatus)status, reason);
}

NetClient *net_client_create(NetClientMessageFn on_message,
                             NetClientStatusFn on_status, void *context)
{
    NetClient *client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }
    client->on_message = on_message;
    client->on_status = on_status;
    client->context = context;
    client->status = NET_CLIENT_IDLE;
    client->reported = (NetClientStatus)-1;
    g_web_client = client;
    return client;
}

void net_client_destroy(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    reversi_web_net_close();
    if (g_web_client == client) {
        g_web_client = NULL;
    }
    free(client);
}

bool net_client_connect(NetClient *client, const char *host, int port)
{
    if (client == NULL || host == NULL || port <= 0 || port > 65535) {
        return false;
    }
    /* A reconnect must not leave the previous socket answering callbacks
     * for the new one. */
    reversi_web_net_close();
    client->head = 0;
    client->count = 0;
    web_set_status(client, NET_CLIENT_CONNECTING, NULL);
    reversi_web_net_connect(host, port);
    return true;
}

void net_client_disconnect(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    reversi_web_net_close();
    web_set_status(client, NET_CLIENT_IDLE, "closed");
}

void net_client_poll(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    if (client->status != client->reported && client->on_status != NULL) {
        client->reported = client->status;
        client->on_status(client->context, client->status, client->reason);
    }
    while (client->count > 0) {
        char text[REVERSI_NET_MAX_MESSAGE + 1];
        snprintf(text, sizeof(text), "%s", client->queue[client->head].text);
        client->head = (client->head + 1) % REVERSI_NET_WEB_QUEUE;
        --client->count;
        NetMessage message;
        if (net_protocol_decode(text, NET_DIRECTION_SERVER_TO_CLIENT,
                                &message) && client->on_message != NULL) {
            client->on_message(client->context, &message);
        }
    }
}

bool net_client_send(NetClient *client, const NetMessage *message)
{
    if (client == NULL || message == NULL
        || client->status != NET_CLIENT_CONNECTED) {
        return false;
    }
    char text[REVERSI_NET_MAX_MESSAGE + 1];
    if (net_protocol_encode(message, text, sizeof(text)) == 0) {
        return false;
    }
    if (reversi_web_net_send(text) == 0) {
        /* The frame cannot be queued (closing socket or full buffer); drop
         * the connection so the session reconnects and resyncs instead of
         * leaving the UI authoritative in its own head. */
        reversi_web_net_close();
        web_set_status(client, NET_CLIENT_FAILED, "send_failed");
        return false;
    }
    return true;
}

/* The browser owns TLS: a https:// page upgrades to wss:// automatically, so
 * there is no separate transport to select and nothing to toggle. */
bool net_client_connect_tls(NetClient *client, const char *host, int port)
{
    return net_client_connect(client, host, port);
}

bool net_client_tls_selectable(void)
{
    return false;
}

NetClientStatus net_client_status(const NetClient *client)
{
    return client != NULL ? client->status : NET_CLIENT_IDLE;
}
