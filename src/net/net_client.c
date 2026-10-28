#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"

#include "net/net_ws.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ClientSocket;
#define CLIENT_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int ClientSocket;
#define CLIENT_INVALID (-1)
#endif

#if defined(_WIN32)
typedef HANDLE ClientThread;
#else
#include <pthread.h>
typedef pthread_t ClientThread;
#endif

#define REVERSI_NET_CLIENT_QUEUE 32
#define REVERSI_NET_CLIENT_SEND_TIMEOUT_MS 1000

typedef struct ClientQueueEntry {
    char text[REVERSI_NET_MAX_MESSAGE + 1];
} ClientQueueEntry;

struct NetClient {
    ClientSocket socket;
    ClientThread thread;
    atomic_bool running;
    atomic_bool stop;
    atomic_bool thread_started;
    atomic_int status;
    atomic_int reported_status;
    char reason[64];
    char host[128];
    int port;
    uint64_t rng;
    NetClientMessageFn on_message;
    NetClientStatusFn on_status;
    void *context;
    ClientQueueEntry queue[REVERSI_NET_CLIENT_QUEUE];
    int queue_head;
    int queue_count;
#if defined(_WIN32)
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    bool mutex_ready;
};

static uint64_t now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static void lock_client(NetClient *client)
{
#if defined(_WIN32)
    EnterCriticalSection(&client->mutex);
#else
    pthread_mutex_lock(&client->mutex);
#endif
}

static void unlock_client(NetClient *client)
{
#if defined(_WIN32)
    LeaveCriticalSection(&client->mutex);
#else
    pthread_mutex_unlock(&client->mutex);
#endif
}

static void socket_close(ClientSocket socket)
{
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

/* 1 = readable, 0 = timeout, -1 = error. */
static int socket_wait_readable(ClientSocket socket, int timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    struct timeval timeout = {.tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000};
    return select((int)socket + 1, &set, NULL, NULL, &timeout);
}

/* 1 = writable, 0 = timeout, -1 = error. */
static int socket_wait_writable(ClientSocket socket, int timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    struct timeval timeout = {.tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000};
    return select((int)socket + 1, NULL, &set, NULL, &timeout);
}

static bool socket_read_would_block(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static bool socket_write_would_block(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static bool socket_nonblocking(ClientSocket socket)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void set_status(NetClient *client, NetClientStatus status,
                       const char *reason)
{
    atomic_store_explicit(&client->status, (int)status, memory_order_release);
    if (reason != NULL) {
        lock_client(client);
        snprintf(client->reason, sizeof(client->reason), "%s", reason);
        unlock_client(client);
    }
}

static void queue_message(NetClient *client, const char *text, size_t length)
{
    if (length > REVERSI_NET_MAX_MESSAGE) {
        return;
    }
    lock_client(client);
    if (client->queue_count >= REVERSI_NET_CLIENT_QUEUE) {
        unlock_client(client);
        set_status(client, NET_CLIENT_FAILED, "queue_overflow");
        return;
    }
    const int index = (client->queue_head + client->queue_count) % REVERSI_NET_CLIENT_QUEUE;
    memcpy(client->queue[index].text, text, length);
    client->queue[index].text[length] = '\0';
    ++client->queue_count;
    unlock_client(client);
}

/* Writes one whole frame or fails within a bounded time. The socket is
 * non-blocking, so a stalled server cannot freeze the UI thread that calls
 * net_client_send(); a partial frame leaves the stream unusable and the
 * caller must drop the connection. */
static bool socket_write_all(NetClient *client, const uint8_t *data,
                             size_t length)
{
    size_t sent = 0;
    const uint64_t deadline = now_ms() + REVERSI_NET_CLIENT_SEND_TIMEOUT_MS;
    while (sent < length) {
        const int written = (int)send(client->socket,
                                      (const char *)data + sent,
                                      (int)(length - sent), 0);
        if (written > 0) {
            sent += (size_t)written;
            continue;
        }
        if (written < 0 && socket_write_would_block()) {
            const uint64_t now = now_ms();
            if (now >= deadline) {
                return false;
            }
            const uint64_t remaining = deadline - now;
            const int slice = remaining < 100 ? (int)remaining : 100;
            if (socket_wait_writable(client->socket, slice) < 0) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

static bool client_handshake(NetClient *client)
{
    char key[25];
    if (!net_ws_generate_key(&client->rng, key)) {
        return false;
    }
    char request[256];
    if (!net_ws_build_handshake_request(client->host, client->port, key,
                                        request)) {
        return false;
    }
    if (!socket_write_all(client, (const uint8_t *)request, strlen(request))) {
        return false;
    }
    char response[1024];
    size_t used = 0;
    const uint64_t deadline = now_ms() + 5000;
    while (used < sizeof(response) - 1 && now_ms() < deadline) {
        const int ready = socket_wait_readable(client->socket, 100);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        const int read = (int)recv(client->socket, response + used,
                                   (int)(sizeof(response) - 1 - used), 0);
        if (read > 0) {
            used += (size_t)read;
            response[used] = '\0';
            if (strstr(response, "\r\n\r\n") != NULL) {
                break;
            }
        } else if (read == 0) {
            return false;
        } else if (!socket_read_would_block()) {
            return false;
        }
    }
    return net_ws_validate_handshake_response(response, used, key);
}

#if defined(_WIN32)
static DWORD WINAPI client_thread(LPVOID argument)
{
    NetClient *client = argument;
#else
static void *client_thread(void *argument)
{
    NetClient *client = argument;
#endif
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char service[16];
    snprintf(service, sizeof(service), "%d", client->port);
    struct addrinfo *results = NULL;
    if (getaddrinfo(client->host, service, &hints, &results) != 0) {
        set_status(client, NET_CLIENT_FAILED, "resolve_failed");
        atomic_store(&client->running, false);
#if defined(_WIN32)
        return 0;
#else
        return NULL;
#endif
    }
    ClientSocket socket_fd = CLIENT_INVALID;
    for (struct addrinfo *it = results; it != NULL; it = it->ai_next) {
        socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_fd == CLIENT_INVALID) {
            continue;
        }
        if (connect(socket_fd, it->ai_addr, (int)it->ai_addrlen) == 0) {
            break;
        }
        socket_close(socket_fd);
        socket_fd = CLIENT_INVALID;
    }
    freeaddrinfo(results);
    if (socket_fd == CLIENT_INVALID) {
        set_status(client, NET_CLIENT_FAILED, "connect_failed");
        atomic_store(&client->running, false);
#if defined(_WIN32)
        return 0;
#else
        return NULL;
#endif
    }
    if (!socket_nonblocking(socket_fd)) {
        set_status(client, NET_CLIENT_FAILED, "connect_failed");
        socket_close(socket_fd);
#if defined(_WIN32)
        return 0;
#else
        return NULL;
#endif
    }
    lock_client(client);
    client->socket = socket_fd;
    unlock_client(client);
    if (!client_handshake(client)) {
        set_status(client, NET_CLIENT_FAILED, "handshake_failed");
        socket_close(socket_fd);
        lock_client(client);
        client->socket = CLIENT_INVALID;
        unlock_client(client);
        atomic_store(&client->running, false);
#if defined(_WIN32)
        return 0;
#else
        return NULL;
#endif
    }
    set_status(client, NET_CLIENT_CONNECTED, NULL);
    NetWsParser parser;
    net_ws_parser_init(&parser);
    uint8_t buffer[4096];
    while (!atomic_load(&client->stop)) {
        /* Selecting first keeps teardown join bounded: a blocked recv is not
         * reliably interrupted by shutdown() from another thread on Windows. */
        const int ready = socket_wait_readable(socket_fd, 20);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            set_status(client, NET_CLIENT_DISCONNECTED, "socket_error");
            break;
        }
        const int read = (int)recv(socket_fd, (char *)buffer,
                                   (int)sizeof(buffer), 0);
        if (read == 0) {
            set_status(client, NET_CLIENT_DISCONNECTED, "closed");
            break;
        }
        if (read < 0) {
            if (socket_read_would_block()) {
                continue;
            }
            set_status(client, NET_CLIENT_DISCONNECTED, "socket_error");
            break;
        }
        size_t offset = 0;
        while (offset < (size_t)read) {
            NetWsFrame frame;
            size_t consumed = 0;
            const NetWsResult result = net_ws_parse(
                &parser, buffer + offset, (size_t)read - offset, false,
                &frame, &consumed);
            if (result == NET_WS_NEED_MORE) {
                if (consumed == 0) {
                    break;
                }
                offset += consumed;
                continue;
            }
            if (result == NET_WS_TEXT) {
                queue_message(client, (const char *)frame.payload,
                              frame.length);
            } else if (result == NET_WS_PING) {
                uint8_t pong[160];
                const size_t length = net_ws_encode(
                    0xA, frame.payload, frame.length, true, &client->rng,
                    pong, sizeof(pong));
                bool written = false;
                if (length > 0) {
                    lock_client(client);
                    written = socket_write_all(client, pong, length);
                    unlock_client(client);
                }
                if (length > 0 && !written) {
                    set_status(client, NET_CLIENT_DISCONNECTED,
                               "send_failed");
                    atomic_store(&client->stop, true);
                    break;
                }
            } else if (result == NET_WS_CLOSE
                       || result == NET_WS_ERROR) {
                set_status(client, NET_CLIENT_DISCONNECTED, "closed");
                atomic_store(&client->stop, true);
                break;
            }
            offset += consumed;
        }
    }
    lock_client(client);
    if (client->socket != CLIENT_INVALID) {
        socket_close(client->socket);
        client->socket = CLIENT_INVALID;
    }
    unlock_client(client);
    atomic_store(&client->running, false);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void thread_join(NetClient *client)
{
    if (!atomic_load(&client->thread_started)) {
        return;
    }
#if defined(_WIN32)
    WaitForSingleObject(client->thread, INFINITE);
    CloseHandle(client->thread);
#else
    pthread_join(client->thread, NULL);
#endif
    atomic_store(&client->running, false);
    atomic_store(&client->thread_started, false);
}

static bool thread_start(NetClient *client)
{
#if defined(_WIN32)
    client->thread = CreateThread(NULL, 0, client_thread, client, 0, NULL);
    return client->thread != NULL;
#else
    return pthread_create(&client->thread, NULL, client_thread, client) == 0;
#endif
}

NetClient *net_client_create(NetClientMessageFn on_message,
                             NetClientStatusFn on_status, void *context)
{
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return NULL;
    }
#endif
    NetClient *client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }
    atomic_init(&client->running, false);
    atomic_init(&client->stop, false);
    atomic_init(&client->thread_started, false);
    atomic_init(&client->status, NET_CLIENT_IDLE);
    atomic_init(&client->reported_status, -1);
    client->socket = CLIENT_INVALID;
    client->on_message = on_message;
    client->on_status = on_status;
    client->context = context;
    client->rng = now_ms() ^ 0x9E3779B97F4A7C15ULL;
    if (client->rng == 0) {
        client->rng = 1;
    }
#if defined(_WIN32)
    InitializeCriticalSection(&client->mutex);
#else
    pthread_mutex_init(&client->mutex, NULL);
#endif
    client->mutex_ready = true;
    return client;
}

void net_client_destroy(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    atomic_store(&client->stop, true);
    lock_client(client);
    if (client->socket != CLIENT_INVALID) {
#if defined(_WIN32)
        shutdown(client->socket, SD_BOTH);
#else
        shutdown(client->socket, SHUT_RDWR);
#endif
    }
    unlock_client(client);
    thread_join(client);
    if (client->mutex_ready) {
#if defined(_WIN32)
        DeleteCriticalSection(&client->mutex);
#else
        pthread_mutex_destroy(&client->mutex);
#endif
    }
    free(client);
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool net_client_connect(NetClient *client, const char *host, int port)
{
    if (client == NULL || host == NULL || port <= 0 || port > 65535) {
        return false;
    }
    net_client_disconnect(client);
    snprintf(client->host, sizeof(client->host), "%s", host);
    client->port = port;
    client->queue_head = 0;
    client->queue_count = 0;
    atomic_store(&client->stop, false);
    atomic_store(&client->running, true);
    atomic_store(&client->reported_status, -1);
    set_status(client, NET_CLIENT_CONNECTING, NULL);
    if (!thread_start(client)) {
        atomic_store(&client->running, false);
        set_status(client, NET_CLIENT_FAILED, "thread_failed");
        return false;
    }
    atomic_store(&client->thread_started, true);
    return true;
}

void net_client_disconnect(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    atomic_store(&client->stop, true);
    lock_client(client);
    if (client->socket != CLIENT_INVALID) {
#if defined(_WIN32)
        shutdown(client->socket, SD_BOTH);
#else
        shutdown(client->socket, SHUT_RDWR);
#endif
    }
    unlock_client(client);
    thread_join(client);
    set_status(client, NET_CLIENT_IDLE, NULL);
}

void net_client_poll(NetClient *client)
{
    if (client == NULL) {
        return;
    }
    const int status = atomic_load_explicit(&client->status,
                                            memory_order_acquire);
    const int reported = atomic_load(&client->reported_status);
    if (status != reported && client->on_status != NULL) {
        atomic_store(&client->reported_status, status);
        char reason[64];
        lock_client(client);
        snprintf(reason, sizeof(reason), "%s", client->reason);
        unlock_client(client);
        client->on_status(client->context, (NetClientStatus)status, reason);
    }
    for (;;) {
        char text[REVERSI_NET_MAX_MESSAGE + 1];
        lock_client(client);
        if (client->queue_count == 0) {
            unlock_client(client);
            break;
        }
        snprintf(text, sizeof(text), "%s", client->queue[client->queue_head].text);
        client->queue_head = (client->queue_head + 1) % REVERSI_NET_CLIENT_QUEUE;
        --client->queue_count;
        unlock_client(client);
        NetMessage message;
        if (net_protocol_decode(text, NET_DIRECTION_SERVER_TO_CLIENT,
                                &message) && client->on_message != NULL) {
            client->on_message(client->context, &message);
        }
    }
}

bool net_client_send(NetClient *client, const NetMessage *message)
{
    if (client == NULL || message == NULL) {
        return false;
    }
    char text[REVERSI_NET_MAX_MESSAGE + 1];
    if (net_protocol_encode(message, text, sizeof(text)) == 0) {
        return false;
    }
    uint8_t frame[REVERSI_NET_MAX_MESSAGE + 20];
    lock_client(client);
    const size_t length = net_ws_encode(
        0x1, (const uint8_t *)text, strlen(text), true, &client->rng, frame,
        sizeof(frame));
    bool ok = false;
    bool failed = false;
    if (length > 0 && client->socket != CLIENT_INVALID
        && atomic_load(&client->status) == NET_CLIENT_CONNECTED) {
        ok = socket_write_all(client, frame, length);
        if (!ok) {
            /* A partial frame corrupts the stream: drop the connection and
             * let the session reconnect and resync from the server. */
            failed = true;
#if defined(_WIN32)
            shutdown(client->socket, SD_BOTH);
#else
            shutdown(client->socket, SHUT_RDWR);
#endif
        }
    }
    unlock_client(client);
    if (failed) {
        set_status(client, NET_CLIENT_DISCONNECTED, "send_failed");
    }
    return ok;
}

NetClientStatus net_client_status(const NetClient *client)
{
    return client != NULL
        ? (NetClientStatus)atomic_load(&client->status) : NET_CLIENT_IDLE;
}
