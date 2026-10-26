#include "net/net_ws.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static bool prefix_nocase(const char *text, const char *prefix, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (tolower((unsigned char)text[i])
            != tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

static bool contains_nocase(const char *text, const char *needle)
{
    const size_t length = strlen(needle);
    for (const char *c = text; *c != '\0'; ++c) {
        if (prefix_nocase(c, needle, length)) {
            return true;
        }
    }
    return false;
}

typedef struct Sha1 {
    uint32_t state[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} Sha1;

static uint32_t rol32(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_block(Sha1 *sha, const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24)
            | ((uint32_t)block[i * 4 + 1] << 16)
            | ((uint32_t)block[i * 4 + 2] << 8)
            | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }
    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
}

static void sha1_init(Sha1 *sha)
{
    sha->state[0] = 0x67452301u;
    sha->state[1] = 0xEFCDAB89u;
    sha->state[2] = 0x98BADCFEu;
    sha->state[3] = 0x10325476u;
    sha->state[4] = 0xC3D2E1F0u;
    sha->length = 0;
    sha->used = 0;
}

static void sha1_update(Sha1 *sha, const uint8_t *data, size_t length)
{
    sha->length += length;
    while (length > 0) {
        const size_t space = 64 - sha->used;
        const size_t take = length < space ? length : space;
        memcpy(sha->block + sha->used, data, take);
        sha->used += take;
        data += take;
        length -= take;
        if (sha->used == 64) {
            sha1_block(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha1_final(Sha1 *sha, uint8_t digest[20])
{
    const uint64_t bits = sha->length * 8;
    uint8_t pad = 0x80;
    sha1_update(sha, &pad, 1);
    uint8_t zero = 0;
    while (sha->used != 56) {
        sha1_update(sha, &zero, 1);
    }
    uint8_t length_bytes[8];
    for (int i = 0; i < 8; ++i) {
        length_bytes[i] = (uint8_t)(bits >> (56 - i * 8));
    }
    sha1_update(sha, length_bytes, 8);
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = (uint8_t)(sha->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(sha->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(sha->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)sha->state[i];
    }
}

static size_t base64_encode(const uint8_t *data, size_t length, char *out,
                            size_t capacity)
{
    static const char ALPHABET[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t needed = ((length + 2) / 3) * 4;
    if (capacity < needed + 1) {
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < length; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < length ? data[i + 1] : 0;
        const uint32_t c = i + 2 < length ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out[written++] = ALPHABET[(triple >> 18) & 0x3F];
        out[written++] = ALPHABET[(triple >> 12) & 0x3F];
        out[written++] = i + 1 < length ? ALPHABET[(triple >> 6) & 0x3F] : '=';
        out[written++] = i + 2 < length ? ALPHABET[triple & 0x3F] : '=';
    }
    out[written] = '\0';
    return written;
}

bool net_ws_accept_key(const char *key, char out[29])
{
    if (key == NULL || out == NULL || strlen(key) != REVERSI_NET_WS_KEY_LEN) {
        return false;
    }
    Sha1 sha;
    sha1_init(&sha);
    sha1_update(&sha, (const uint8_t *)key, strlen(key));
    sha1_update(&sha, (const uint8_t *)WS_GUID, strlen(WS_GUID));
    uint8_t digest[20];
    sha1_final(&sha, digest);
    return base64_encode(digest, sizeof(digest), out, 29) == 28;
}

static bool header_value(const char *request, const char *name,
                         char *out, size_t capacity)
{
    const size_t name_length = strlen(name);
    const char *line = strstr(request, "\r\n");
    while (line != NULL) {
        line += 2;
        const char *end = strstr(line, "\r\n");
        if (end == NULL) {
            break;
        }
        if ((size_t)(end - line) > name_length && line[name_length] == ':'
            && prefix_nocase(line, name, name_length)) {
            const char *value = line + name_length + 1;
            while (value < end && (*value == ' ' || *value == '\t')) {
                ++value;
            }
            size_t length = (size_t)(end - value);
            while (length > 0 && (value[length - 1] == ' '
                                  || value[length - 1] == '\t')) {
                --length;
            }
            if (length + 1 > capacity) {
                return false;
            }
            memcpy(out, value, length);
            out[length] = '\0';
            return true;
        }
        line = end;
    }
    return false;
}

bool net_ws_parse_handshake(const char *request, size_t length, char key[25])
{
    if (request == NULL || length < 32 || key == NULL
        || memchr(request, '\0', length) != NULL) {
        return false;
    }
    char buffer[4096];
    if (length >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, request, length);
    buffer[length] = '\0';
    if (strncmp(buffer, "GET ", 4) != 0
        || strstr(buffer, "\r\n\r\n") == NULL) {
        return false;
    }
    char value[128];
    if (!header_value(buffer, "Upgrade", value, sizeof(value))
        || !(prefix_nocase(value, "websocket", 9) && value[9] == '\0')) {
        return false;
    }
    if (!header_value(buffer, "Connection", value, sizeof(value))
        || !contains_nocase(value, "upgrade")) {
        return false;
    }
    if (!header_value(buffer, "Sec-WebSocket-Version", value, sizeof(value))
        || strcmp(value, "13") != 0) {
        return false;
    }
    if (!header_value(buffer, "Sec-WebSocket-Key", value, sizeof(value))
        || strlen(value) != REVERSI_NET_WS_KEY_LEN) {
        return false;
    }
    memcpy(key, value, REVERSI_NET_WS_KEY_LEN + 1);
    return true;
}

bool net_ws_build_handshake_response(const char *key, char out[256])
{
    char accept[29];
    if (!net_ws_accept_key(key, accept)) {
        return false;
    }
    const int written = snprintf(
        out, 256,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    return written > 0 && written < 256;
}

bool net_ws_build_handshake_request(const char *host, int port,
                                    const char *key, char out[256])
{
    if (host == NULL || key == NULL
        || strlen(key) != REVERSI_NET_WS_KEY_LEN) {
        return false;
    }
    const int written = snprintf(
        out, 256,
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n", host, port, key);
    return written > 0 && written < 256;
}

bool net_ws_validate_handshake_response(const char *response, size_t length,
                                        const char *key)
{
    if (response == NULL || key == NULL || length < 32) {
        return false;
    }
    char buffer[2048];
    if (length >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, response, length);
    buffer[length] = '\0';
    if (strncmp(buffer, "HTTP/1.1 101", 12) != 0) {
        return false;
    }
    char value[128];
    char expected[29];
    if (!net_ws_accept_key(key, expected)
        || !header_value(buffer, "Sec-WebSocket-Accept", value,
                         sizeof(value))) {
        return false;
    }
    return strcmp(value, expected) == 0;
}

static uint64_t ws_rng_next(uint64_t *rng)
{
    uint64_t x = *rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

bool net_ws_generate_key(uint64_t *rng, char out[25])
{
    if (rng == NULL || out == NULL) {
        return false;
    }
    uint8_t bytes[16];
    for (int i = 0; i < 16; i += 8) {
        const uint64_t value = ws_rng_next(rng);
        for (int j = 0; j < 8; ++j) {
            bytes[i + j] = (uint8_t)(value >> (j * 8));
        }
    }
    return base64_encode(bytes, sizeof(bytes), out, 25) == 24;
}

void net_ws_parser_init(NetWsParser *parser)
{
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
    }
}

static bool is_control(uint8_t opcode)
{
    return opcode == 0x8 || opcode == 0x9 || opcode == 0xA;
}

NetWsResult net_ws_parse(NetWsParser *parser, const uint8_t *data,
                         size_t length, bool require_mask, NetWsFrame *out,
                         size_t *consumed)
{
    if (parser == NULL || data == NULL || out == NULL || consumed == NULL) {
        return NET_WS_ERROR;
    }
    *consumed = 0;
    if (length < 2) {
        return NET_WS_NEED_MORE;
    }
    const uint8_t first = data[0];
    const uint8_t second = data[1];
    const bool fin = (first & 0x80) != 0;
    const uint8_t opcode = first & 0x0F;
    const bool masked = (second & 0x80) != 0;
    const uint8_t length_code = second & 0x7F;
    if ((first & 0x70) != 0 || (require_mask && !masked)
        || (!require_mask && masked)) {
        return NET_WS_ERROR;
    }
    size_t header = 2;
    uint64_t payload_length = length_code;
    if (length_code == 126) {
        header += 2;
        if (length < header) {
            return NET_WS_NEED_MORE;
        }
        payload_length = ((uint64_t)data[2] << 8) | data[3];
        if (payload_length < 126) {
            return NET_WS_ERROR;
        }
    } else if (length_code == 127) {
        header += 8;
        if (length < header) {
            return NET_WS_NEED_MORE;
        }
        payload_length = 0;
        for (int i = 0; i < 8; ++i) {
            payload_length = (payload_length << 8) | data[2 + i];
        }
        if (payload_length < 65536
            || (payload_length >> 63) != 0) {
            return NET_WS_ERROR;
        }
    }
    if (masked) {
        header += 4;
        if (length < header) {
            return NET_WS_NEED_MORE;
        }
    }
    if (payload_length > REVERSI_NET_WS_MAX_PAYLOAD) {
        return NET_WS_ERROR;
    }
    if (length < header + (size_t)payload_length) {
        return NET_WS_NEED_MORE;
    }

    const uint8_t *payload = data + header;
    uint8_t *target = NULL;
    if (is_control(opcode)) {
        if (!fin || payload_length > sizeof(parser->control)) {
            return NET_WS_ERROR;
        }
        target = parser->control;
    } else if (opcode == 0x1 || opcode == 0x0) {
        if (opcode == 0x1 && parser->in_message) {
            return NET_WS_ERROR;
        }
        if (opcode == 0x0 && !parser->in_message) {
            return NET_WS_ERROR;
        }
        if (opcode == 0x1) {
            parser->message_length = 0;
            parser->in_message = true;
        }
        if (parser->message_length + payload_length
            > REVERSI_NET_WS_MAX_PAYLOAD) {
            return NET_WS_ERROR;
        }
        target = parser->message + parser->message_length;
    } else {
        return NET_WS_ERROR;
    }

    if (masked) {
        const uint8_t *mask = data + header - 4;
        for (size_t i = 0; i < (size_t)payload_length; ++i) {
            target[i] = payload[i] ^ mask[i & 3];
        }
    } else {
        memcpy(target, payload, (size_t)payload_length);
    }
    *consumed = header + (size_t)payload_length;

    if (is_control(opcode)) {
        out->payload = parser->control;
        out->length = (size_t)payload_length;
        out->type = opcode == 0x8 ? NET_WS_CLOSE
            : opcode == 0x9 ? NET_WS_PING : NET_WS_PONG;
        return out->type;
    }

    parser->message_length += (size_t)payload_length;
    if (!fin) {
        return NET_WS_NEED_MORE;
    }
    parser->in_message = false;
    out->payload = parser->message;
    out->length = parser->message_length;
    out->type = NET_WS_TEXT;
    return NET_WS_TEXT;
}

size_t net_ws_encode(uint8_t opcode, const uint8_t *payload, size_t length,
                     bool mask, uint64_t *rng, uint8_t *out, size_t capacity)
{
    if (out == NULL || (payload == NULL && length > 0)
        || length > REVERSI_NET_WS_MAX_PAYLOAD
        || (is_control(opcode) && length > 125)
        || (mask && rng == NULL)) {
        return 0;
    }
    size_t header = 2;
    if (length >= 65536) {
        header += 8;
    } else if (length >= 126) {
        header += 2;
    }
    if (mask) {
        header += 4;
    }
    if (header + length > capacity) {
        return 0;
    }
    out[0] = (uint8_t)(0x80 | opcode);
    if (length >= 65536) {
        out[1] = 127;
        for (int i = 0; i < 8; ++i) {
            out[2 + i] = (uint8_t)((uint64_t)length >> (56 - i * 8));
        }
    } else if (length >= 126) {
        out[1] = 126;
        out[2] = (uint8_t)(length >> 8);
        out[3] = (uint8_t)length;
    } else {
        out[1] = (uint8_t)length;
    }
    size_t offset = header - (mask ? 4 : 0);
    if (mask) {
        out[1] |= 0x80;
        const uint64_t key = ws_rng_next(rng);
        for (int i = 0; i < 4; ++i) {
            out[offset + i] = (uint8_t)(key >> (i * 8));
        }
        for (size_t i = 0; i < length; ++i) {
            out[header + i] = payload[i] ^ out[offset + (i & 3)];
        }
    } else {
        if (length > 0) {
            memcpy(out + header, payload, length);
        }
    }
    return header + length;
}
