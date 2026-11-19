#ifndef REVERSI_TESTS_TEST_SOCKET_H
#define REVERSI_TESTS_TEST_SOCKET_H

/*
 * Raw loopback sockets shared by the net tests, for playing the other end
 * of a WebSocket connection by hand. On POSIX, include it after defining
 * _POSIX_C_SOURCE. On Windows, create a NetClient/NetSession or server
 * first: that performs WSAStartup, which must precede any socket() call.
 */

#include "net/net_ws.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET TestSocket;
#define REVERSI_TEST_SOCKET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int TestSocket;
#define REVERSI_TEST_SOCKET_INVALID (-1)
#endif

static inline void test_sleep_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = {.tv_sec = ms / 1000,
                          .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static inline void test_close_socket(TestSocket socket_fd)
{
#if defined(_WIN32)
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

/* Sends every byte or returns false. */
static inline bool test_send_raw(TestSocket socket_fd, const void *data,
                                 size_t length)
{
    const char *bytes = data;
    size_t sent = 0;
    while (sent < length) {
        const int written = (int)send(socket_fd, bytes + sent,
                                      (int)(length - sent), 0);
        if (written <= 0) {
            return false;
        }
        sent += (size_t)written;
    }
    return true;
}

/* Listens on an ephemeral 127.0.0.1 port. receive_buffer > 0 sets SO_RCVBUF
 * before listen() so accepted sockets start with it. */
static inline TestSocket test_listen_loopback(int receive_buffer, int *port)
{
    TestSocket listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == REVERSI_TEST_SOCKET_INVALID) {
        return listener;
    }
    if (receive_buffer > 0) {
        setsockopt(listener, SOL_SOCKET, SO_RCVBUF,
                   (const char *)&receive_buffer, sizeof(receive_buffer));
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
#if defined(_WIN32)
    int address_length = (int)sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0
        || listen(listener, 1) != 0
        || getsockname(listener, (struct sockaddr *)&address,
                       &address_length) != 0) {
        test_close_socket(listener);
        return REVERSI_TEST_SOCKET_INVALID;
    }
    *port = ntohs(address.sin_port);
    return listener;
}

/* Accepts one client and reads its WebSocket upgrade request. The matching
 * 101 response is written to response but not sent, so the caller decides
 * how it reaches the wire. */
static inline TestSocket test_accept_websocket(TestSocket listener,
                                               char response[256])
{
    TestSocket server = accept(listener, NULL, NULL);
    if (server == REVERSI_TEST_SOCKET_INVALID) {
        return server;
    }
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
    if (!net_ws_parse_handshake(request, used, key)
        || !net_ws_build_handshake_response(key, response)) {
        test_close_socket(server);
        return REVERSI_TEST_SOCKET_INVALID;
    }
    return server;
}

#endif
