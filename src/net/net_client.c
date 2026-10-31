#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/net_client.h"

#include "net/net_ws.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(REVERSI_NET_TLS) && REVERSI_NET_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#define REVERSI_NET_CLIENT_TLS 1
#else
#define REVERSI_NET_CLIENT_TLS 0
#endif

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
/* Outbound frames wait here until the worker thread can write them. Sized
 * like the server's per-connection queue: a stalled peer fills it in a few
 * dozen moves, long before latency becomes the user's problem. */
#define REVERSI_NET_CLIENT_SEND_QUEUE_BYTES 16384
#define REVERSI_NET_CLIENT_SEND_TIMEOUT_MS 1000
#define REVERSI_NET_CLIENT_CONNECT_TIMEOUT_MS 5000

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
    /* Outbound bytes: written by the caller (any thread) and drained by the
     * worker, always under the mutex. */
    uint8_t outbound[REVERSI_NET_CLIENT_SEND_QUEUE_BYTES];
    size_t outbound_head;
    size_t outbound_count;
    /* TLS (wss://). The handles live on the worker; the flag is set before
     * the thread starts and read only by it until teardown. */
    bool use_tls;
    void *tls;      /* SSL * */
    void *tls_ctx;  /* SSL_CTX * */
    /* Length of the last SSL_write that returned WANT; OpenSSL requires the
     * retry to repeat the exact same call. Zero when nothing is pending. */
    size_t tls_pending;
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

static bool socket_would_block(void)
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

static bool socket_in_progress(void)
{
#if defined(_WIN32)
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS
        || error == WSAEALREADY;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK
        || errno == EALREADY;
#endif
}

/* True when the asynchronous connect reported an error. */
static bool socket_connect_failed(ClientSocket socket_fd)
{
    int error = 0;
#if defined(_WIN32)
    int length = (int)sizeof(error);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (char *)&error,
                   &length) != 0) {
        return true;
    }
#else
    socklen_t length = sizeof(error);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
        return true;
    }
#endif
    return error != 0;
}

/* Waits for an asynchronous connect, aborting within 100 ms when the caller
 * asks the client to stop. */
static bool socket_wait_connected(NetClient *client, ClientSocket socket_fd,
                                  uint64_t deadline)
{
    for (;;) {
        if (atomic_load(&client->stop)) {
            return false;
        }
        const uint64_t now = now_ms();
        if (now >= deadline) {
            return false;
        }
        const uint64_t remaining = deadline - now;
        const int slice = remaining < 100 ? (int)remaining : 100;
        const int ready = socket_wait_writable(socket_fd, slice);
        if (ready < 0) {
            return false;
        }
        if (ready > 0) {
            return !socket_connect_failed(socket_fd);
        }
    }
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

/* Connection I/O: >0 bytes, 0 on EOF, -1 on error, -2 when the socket or the
 * TLS state cannot make progress yet. Both transports share the callers. */
static int connection_read(NetClient *client, void *data, size_t length)
{
#if REVERSI_NET_CLIENT_TLS
    if (client->tls != NULL) {
        SSL *ssl = (SSL *)client->tls;
        const int read = SSL_read(ssl, data, (int)length);
        if (read > 0) {
            return read;
        }
        const int error = SSL_get_error(ssl, read);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            return -2;
        }
        if (error == SSL_ERROR_ZERO_RETURN) {
            return 0;
        }
        return -1;
    }
#endif
    const int read = (int)recv(client->socket, (char *)data, (int)length, 0);
    if (read >= 0) {
        return read;
    }
    return socket_would_block() ? -2 : -1;
}

static int connection_write(NetClient *client, const void *data, size_t length)
{
#if REVERSI_NET_CLIENT_TLS
    if (client->tls != NULL) {
        SSL *ssl = (SSL *)client->tls;
        const int written = SSL_write(ssl, data, (int)length);
        if (written > 0) {
            return written;
        }
        const int error = SSL_get_error(ssl, written);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            return -2;
        }
        return -1;
    }
#endif
    const int written = (int)send(client->socket, (const char *)data,
                                  (int)length, 0);
    if (written >= 0) {
        return written;
    }
    return socket_would_block() ? -2 : -1;
}

#if REVERSI_NET_CLIENT_TLS
static void tls_release(NetClient *client)
{
    if (client->tls != NULL) {
        SSL_shutdown((SSL *)client->tls);
        SSL_free((SSL *)client->tls);
        client->tls = NULL;
    }
    if (client->tls_ctx != NULL) {
        SSL_CTX_free((SSL_CTX *)client->tls_ctx);
        client->tls_ctx = NULL;
    }
    client->tls_pending = 0;
}

/* Verifies the chain against the system store (or SSL_CERT_FILE) and the
 * host name against the address the user typed; there is no switch to skip
 * either check. */
static bool tls_handshake(NetClient *client, const char **reason)
{
    if (client->tls_ctx == NULL) {
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (ctx == NULL) {
            *reason = "tls_unavailable";
            return false;
        }
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION);
        (void)SSL_CTX_set_default_verify_paths(ctx);
        client->tls_ctx = ctx;
    }
    SSL *ssl = SSL_new((SSL_CTX *)client->tls_ctx);
    if (ssl == NULL) {
        *reason = "tls_unavailable";
        return false;
    }
    client->tls = ssl;
    SSL_set_fd(ssl, (int)client->socket);
    /* An IP literal must be checked against the certificate's IP SAN:
     * SSL_set1_host() only understands DNS names and would reject a peer
     * that dials 127.0.0.1 (the lobby default). */
    struct in_addr literal4;
    struct in6_addr literal6;
    if (inet_pton(AF_INET, client->host, &literal4) == 1
        || inet_pton(AF_INET6, client->host, &literal6) == 1) {
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),
                                          client->host) != 1) {
            *reason = "tls_certificate_invalid";
            return false;
        }
    } else {
        SSL_set_tlsext_host_name(ssl, client->host);
        if (SSL_set1_host(ssl, client->host) != 1) {
            *reason = "tls_certificate_invalid";
            return false;
        }
    }
    SSL_set_connect_state(ssl);
    const uint64_t deadline = now_ms() + REVERSI_NET_CLIENT_CONNECT_TIMEOUT_MS;
    for (;;) {
        if (atomic_load(&client->stop)) {
            *reason = "cancelled";
            return false;
        }
        const int result = SSL_connect(ssl);
        if (result == 1) {
            return true;
        }
        const int error = SSL_get_error(ssl, result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            *reason = SSL_get_verify_result(ssl) == X509_V_OK
                ? "tls_handshake_failed" : "tls_certificate_invalid";
            return false;
        }
        const uint64_t now = now_ms();
        if (now >= deadline) {
            *reason = "tls_handshake_failed";
            return false;
        }
        const uint64_t remaining = deadline - now;
        const int slice = remaining < 100 ? (int)remaining : 100;
        if (error == SSL_ERROR_WANT_READ) {
            if (socket_wait_readable(client->socket, slice) < 0) {
                *reason = "tls_handshake_failed";
                return false;
            }
        } else if (socket_wait_writable(client->socket, slice) < 0) {
            *reason = "tls_handshake_failed";
            return false;
        }
    }
}
#endif

/* Writes one whole frame or fails within a bounded time. Only the worker
 * thread uses this (connect handshake); application frames go through the
 * outbound queue so no UI thread ever waits for the socket. */
static bool socket_write_all(NetClient *client, const uint8_t *data,
                             size_t length)
{
    size_t sent = 0;
    const uint64_t deadline = now_ms() + REVERSI_NET_CLIENT_SEND_TIMEOUT_MS;
    while (sent < length) {
        const int written = connection_write(client, data + sent, length - sent);
        if (written > 0) {
            sent += (size_t)written;
            continue;
        }
        if (written == -2) {
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

/* Appends a frame to the outbound ring. The mutex must be held. */
static bool outbound_push(NetClient *client, const uint8_t *data,
                          size_t length)
{
    const size_t capacity = sizeof(client->outbound);
    if (length > capacity - client->outbound_count) {
        return false;
    }
    const size_t tail = (client->outbound_head + client->outbound_count)
        % capacity;
    const size_t first = capacity - tail;
    const size_t head_length = length < first ? length : first;
    memcpy(client->outbound + tail, data, head_length);
    if (length > head_length) {
        memcpy(client->outbound, data + head_length, length - head_length);
    }
    client->outbound_count += length;
    return true;
}

/* Writes what the socket accepts, keeping the rest queued. Returns false
 * only when the stream is unusable. The mutex must be held. */
static bool outbound_flush(NetClient *client)
{
    const size_t capacity = sizeof(client->outbound);
    while (client->outbound_count > 0
           && client->socket != CLIENT_INVALID) {
        /* TLS requires the retry to repeat the exact same call, so a
         * would-block write keeps its length until it succeeds. */
        size_t length = client->tls_pending;
        if (length == 0) {
            const size_t contiguous = capacity - client->outbound_head;
            length = client->outbound_count < contiguous
                ? client->outbound_count : contiguous;
        }
        const int written = connection_write(
            client, client->outbound + client->outbound_head, length);
        if (written > 0) {
            client->tls_pending = 0;
            client->outbound_head =
                (client->outbound_head + (size_t)written) % capacity;
            client->outbound_count -= (size_t)written;
            continue;
        }
        if (written == -2) {
            client->tls_pending = length;
            return true;
        }
        return false;
    }
    return true;
}

static void outbound_clear(NetClient *client)
{
    client->outbound_head = 0;
    client->outbound_count = 0;
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
    while (used < sizeof(response) - 1 && now_ms() < deadline
           && !atomic_load(&client->stop)) {
        const int ready = socket_wait_readable(client->socket, 100);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        const int read = connection_read(client, response + used,
                                         sizeof(response) - 1 - used);
        if (read > 0) {
            used += (size_t)read;
            response[used] = '\0';
            if (strstr(response, "\r\n\r\n") != NULL) {
                break;
            }
        } else if (read == 0) {
            return false;
        } else if (read != -2) {
            return false;
        }
    }
    return net_ws_validate_handshake_response(response, used, key);
}

/* Plaintext (ws://) is only ever sent to loopback or private ranges, so the
 * protocol is never put on the wire in clear text towards a public address.
 * wss:// connections skip this check because the certificate is verified.
 * Refusing here — before any socket is opened — keeps the rule enforceable
 * without a dial-out to check reachability. IPv4 only: the client resolves
 * AF_INET, so IPv6 literals never reach this point. */
static bool address_is_local(const struct sockaddr *address)
{
    if (address == NULL || address->sa_family != AF_INET) {
        return false;
    }
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    const uint32_t host = ntohl(in->sin_addr.s_addr);
    return (host >> 24) == 127      /* 127/8 loopback */
        || (host >> 24) == 10       /* 10/8 private */
        || (host >> 20) == 0xAC1    /* 172.16/12 private */
        || (host >> 16) == 0xC0A8   /* 192.168/16 private */
        || (host >> 16) == 0xA9FE   /* 169.254/16 link-local */
        || (host >> 22) == 0x191;   /* 100.64/10 carrier NAT */
}

#if defined(_WIN32)
#define CLIENT_THREAD_RETURN return 0
#else
#define CLIENT_THREAD_RETURN return NULL
#endif

/* Ends the worker after a failed connection: reports the reason (unless the
 * caller already stopped the client), releases TLS, drops whatever was
 * queued and marks the thread finished. */
static void client_fail(NetClient *client, ClientSocket socket_fd,
                        const char *reason)
{
    if (reason != NULL && !atomic_load(&client->stop)) {
        set_status(client, NET_CLIENT_FAILED, reason);
    }
#if REVERSI_NET_CLIENT_TLS
    tls_release(client);
#endif
    lock_client(client);
    outbound_clear(client);
    if (socket_fd != CLIENT_INVALID) {
        socket_close(socket_fd);
    }
    client->socket = CLIENT_INVALID;
    unlock_client(client);
    atomic_store(&client->running, false);
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
    /* Plaintext is only ever sent to loopback/private addresses; with TLS
     * any address is allowed because the certificate is verified. */
    bool allowed = client->use_tls;
    for (const struct addrinfo *it = results; it != NULL && !allowed;
         it = it->ai_next) {
        if (address_is_local(it->ai_addr)) {
            allowed = true;
        }
    }
    if (!allowed) {
        freeaddrinfo(results);
        client_fail(client, CLIENT_INVALID, "tls_required");
        CLIENT_THREAD_RETURN;
    }
    ClientSocket socket_fd = CLIENT_INVALID;
    const uint64_t connect_deadline =
        now_ms() + REVERSI_NET_CLIENT_CONNECT_TIMEOUT_MS;
    for (struct addrinfo *it = results; it != NULL; it = it->ai_next) {
        if (!client->use_tls && !address_is_local(it->ai_addr)) {
            continue;
        }
        socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_fd == CLIENT_INVALID) {
            continue;
        }
        if (!socket_nonblocking(socket_fd)) {
            socket_close(socket_fd);
            socket_fd = CLIENT_INVALID;
            continue;
        }
        if (connect(socket_fd, it->ai_addr, (int)it->ai_addrlen) == 0) {
            break;
        }
        if (socket_in_progress()
            && socket_wait_connected(client, socket_fd, connect_deadline)) {
            break;
        }
        socket_close(socket_fd);
        socket_fd = CLIENT_INVALID;
        if (atomic_load(&client->stop)) {
            break;
        }
    }
    freeaddrinfo(results);
    if (socket_fd == CLIENT_INVALID) {
        client_fail(client, CLIENT_INVALID, "connect_failed");
        CLIENT_THREAD_RETURN;
    }
    lock_client(client);
    client->socket = socket_fd;
    unlock_client(client);
#if REVERSI_NET_CLIENT_TLS
    if (client->use_tls) {
        const char *reason = "tls_handshake_failed";
        if (!tls_handshake(client, &reason)) {
            client_fail(client, socket_fd, reason);
            CLIENT_THREAD_RETURN;
        }
    }
#else
    if (client->use_tls) {
        client_fail(client, socket_fd, "tls_unavailable");
        CLIENT_THREAD_RETURN;
    }
#endif
    if (!client_handshake(client)) {
        client_fail(client, socket_fd, "handshake_failed");
        CLIENT_THREAD_RETURN;
    }
    set_status(client, NET_CLIENT_CONNECTED, NULL);
    NetWsParser parser;
    net_ws_parser_init(&parser);
    uint8_t buffer[4096];
    while (!atomic_load(&client->stop)) {
        /* Drain the outbound queue before sleeping: queued frames are at
         * most one poll interval (20 ms) old. */
        lock_client(client);
        const bool flushed = outbound_flush(client);
        unlock_client(client);
        if (!flushed) {
            set_status(client, NET_CLIENT_DISCONNECTED, "send_failed");
            break;
        }
        /* Selecting keeps teardown join bounded: a blocked recv is not
         * reliably interrupted by shutdown() from another thread on Windows. */
        const int ready = socket_wait_readable(socket_fd, 20);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            set_status(client, NET_CLIENT_DISCONNECTED, "socket_error");
            break;
        }
        const int read = connection_read(client, buffer, sizeof(buffer));
        if (read == 0) {
            /* A shutdown this client asked for already carries its own
             * reason; do not overwrite it with "closed". */
            if (!atomic_load(&client->stop)) {
                set_status(client, NET_CLIENT_DISCONNECTED, "closed");
            }
            break;
        }
        if (read < 0) {
            if (read == -2) {
                continue;
            }
            if (!atomic_load(&client->stop)) {
                set_status(client, NET_CLIENT_DISCONNECTED, "socket_error");
            }
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
                    written = outbound_push(client, pong, length)
                        && outbound_flush(client);
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
#if REVERSI_NET_CLIENT_TLS
    tls_release(client);
#endif
    lock_client(client);
    outbound_clear(client);
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

static bool client_connect(NetClient *client, const char *host, int port,
                           bool use_tls)
{
    if (client == NULL || host == NULL || port <= 0 || port > 65535) {
        return false;
    }
    net_client_disconnect(client);
    /* Fail fast rather than silently downgrading to plaintext. */
    if (use_tls && !REVERSI_NET_CLIENT_TLS) {
        set_status(client, NET_CLIENT_FAILED, "tls_unavailable");
        return false;
    }
    snprintf(client->host, sizeof(client->host), "%s", host);
    client->port = port;
    client->use_tls = use_tls;
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

bool net_client_connect(NetClient *client, const char *host, int port)
{
    return client_connect(client, host, port, false);
}

bool net_client_connect_tls(NetClient *client, const char *host, int port)
{
    return client_connect(client, host, port, true);
}

bool net_client_tls_selectable(void)
{
    return REVERSI_NET_CLIENT_TLS != 0;
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
    /* Drain queued messages before reporting a status change: a peer that
     * sends a final state and then closes would otherwise be seen as
     * "disconnected" before the state it just sent. */
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
    bool queued = false;
    bool overflow = false;
    ClientSocket socket = client->socket;
    if (length > 0 && socket != CLIENT_INVALID
        && atomic_load(&client->status) == NET_CLIENT_CONNECTED) {
        queued = outbound_push(client, frame, length);
        overflow = !queued;
    }
    if (overflow && socket != CLIENT_INVALID) {
        /* Shut the socket down while the mutex still pins it: the descriptor
         * could otherwise be closed and reused by the worker before this
         * call reaches shutdown(). */
#if defined(_WIN32)
        shutdown(socket, SD_BOTH);
#else
        shutdown(socket, SHUT_RDWR);
#endif
    }
    unlock_client(client);
    if (overflow) {
        /* The queue is the only buffer between this call and a peer that
         * stopped reading. Once it is full the frame cannot arrive in time,
         * so drop the connection and let the session resync. */
        set_status(client, NET_CLIENT_DISCONNECTED, "send_queue_full");
        atomic_store(&client->stop, true);
    }
    return queued;
}

NetClientStatus net_client_status(const NetClient *client)
{
    return client != NULL
        ? (NetClientStatus)atomic_load(&client->status) : NET_CLIENT_IDLE;
}
