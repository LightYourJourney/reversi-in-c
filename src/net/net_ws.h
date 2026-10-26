#ifndef REVERSI_NET_NET_WS_H
#define REVERSI_NET_NET_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REVERSI_NET_WS_MAX_PAYLOAD 2048
#define REVERSI_NET_WS_MAX_HEADER 14
#define REVERSI_NET_WS_KEY_LEN 24
#define REVERSI_NET_WS_ACCEPT_LEN 28

typedef enum NetWsResult {
    NET_WS_NEED_MORE = 0,
    NET_WS_TEXT,
    NET_WS_PING,
    NET_WS_PONG,
    NET_WS_CLOSE,
    NET_WS_ERROR
} NetWsResult;

typedef struct NetWsParser {
    uint8_t message[REVERSI_NET_WS_MAX_PAYLOAD + 1];
    size_t message_length;
    bool in_message;
    uint8_t control[125];
} NetWsParser;

typedef struct NetWsFrame {
    NetWsResult type;
    const uint8_t *payload;
    size_t length;
} NetWsFrame;

void net_ws_parser_init(NetWsParser *parser);

/*
 * Parses one frame from the start of data. require_mask is true when parsing
 * client frames on the server. Returns NEED_MORE with consumed 0 when the
 * buffer does not hold a complete frame; otherwise consumed is the number of
 * bytes used. Fragmented text is reassembled into parser->message.
 */
NetWsResult net_ws_parse(NetWsParser *parser, const uint8_t *data,
                         size_t length, bool require_mask, NetWsFrame *out,
                         size_t *consumed);

/*
 * Encodes one frame. Lengths above REVERSI_NET_WS_MAX_PAYLOAD are rejected.
 * Masking uses a key drawn from rng (the caller owns it).
 */
size_t net_ws_encode(uint8_t opcode, const uint8_t *payload, size_t length,
                     bool mask, uint64_t *rng, uint8_t *out, size_t capacity);

/* RFC 6455 handshake helpers. */
bool net_ws_accept_key(const char *key, char out[29]);
bool net_ws_parse_handshake(const char *request, size_t length, char key[25]);
bool net_ws_build_handshake_response(const char *key, char out[256]);
bool net_ws_build_handshake_request(const char *host, int port,
                                    const char *key, char out[256]);
bool net_ws_validate_handshake_response(const char *response, size_t length,
                                        const char *key);
bool net_ws_generate_key(uint64_t *rng, char out[25]);

#endif
