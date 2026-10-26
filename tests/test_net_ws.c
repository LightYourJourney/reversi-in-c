#include "net/net_ws.h"
#include "test_harness.h"

#include <stdint.h>
#include <string.h>

static void test_accept_key(TestStats *stats)
{
    char accept[29];
    REVERSI_TEST_ASSERT(stats, net_ws_accept_key(
        "dGhlIHNhbXBsZSBub25jZQ==", accept));
    REVERSI_TEST_ASSERT(stats,
        strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

    const char *request =
        "GET /chat HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    char key[25];
    REVERSI_TEST_ASSERT(stats, net_ws_parse_handshake(
        request, strlen(request), key));
    REVERSI_TEST_ASSERT(stats, strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);
    char response[256];
    REVERSI_TEST_ASSERT(stats, net_ws_build_handshake_response(key, response));
    REVERSI_TEST_ASSERT(stats, strstr(response,
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL);
    REVERSI_TEST_ASSERT(stats, net_ws_validate_handshake_response(
        response, strlen(response), key));
}

static void test_frames(TestStats *stats)
{
    uint64_t rng = 1;
    uint8_t frame[4096];
    NetWsParser parser;
    NetWsFrame out;
    size_t consumed = 0;

    const char *text = "hello world";
    size_t length = net_ws_encode(0x1, (const uint8_t *)text, strlen(text),
                                  false, NULL, frame, sizeof(frame));
    REVERSI_TEST_ASSERT(stats, length > 0);
    net_ws_parser_init(&parser);
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame, length, false, &out, &consumed) == NET_WS_TEXT);
    REVERSI_TEST_ASSERT(stats, consumed == length);
    REVERSI_TEST_ASSERT(stats, out.length == strlen(text));
    REVERSI_TEST_ASSERT(stats, memcmp(out.payload, text, out.length) == 0);

    length = net_ws_encode(0x1, (const uint8_t *)text, strlen(text), true,
                           &rng, frame, sizeof(frame));
    REVERSI_TEST_ASSERT(stats, length > 0);
    REVERSI_TEST_ASSERT(stats, (frame[1] & 0x80) != 0);
    net_ws_parser_init(&parser);
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame, length, true, &out, &consumed) == NET_WS_TEXT);
    REVERSI_TEST_ASSERT(stats, memcmp(out.payload, text, out.length) == 0);
    net_ws_parser_init(&parser);
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame, length, false, &out, &consumed) == NET_WS_ERROR);

    /* Control frame. */
    length = net_ws_encode(0x9, (const uint8_t *)"p", 1, true, &rng,
                           frame, sizeof(frame));
    net_ws_parser_init(&parser);
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame, length, true, &out, &consumed) == NET_WS_PING);
    REVERSI_TEST_ASSERT(stats, out.length == 1 && out.payload[0] == 'p');
}

static void test_fragmentation(TestStats *stats)
{
    uint64_t rng = 7;
    uint8_t frame_a[128];
    uint8_t frame_b[128];
    const char *first = "hel";
    const char *second = "lo";
    size_t length_a = net_ws_encode(0x1, (const uint8_t *)first, 3, true,
                                    &rng, frame_a, sizeof(frame_a));
    /* net_ws_encode always sets FIN; build a fragmented first frame by hand. */
    frame_a[0] = 0x01;
    size_t length_b = net_ws_encode(0x0, (const uint8_t *)second, 2, true,
                                    &rng, frame_b, sizeof(frame_b));
    NetWsParser parser;
    net_ws_parser_init(&parser);
    NetWsFrame out;
    size_t consumed = 0;
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame_a, length_a, true, &out, &consumed) == NET_WS_NEED_MORE);
    REVERSI_TEST_ASSERT(stats, consumed == length_a);
    frame_b[0] = 0x80;
    REVERSI_TEST_ASSERT(stats, net_ws_parse(
        &parser, frame_b, length_b, true, &out, &consumed) == NET_WS_TEXT);
    REVERSI_TEST_ASSERT(stats, out.length == 5);
    REVERSI_TEST_ASSERT(stats, memcmp(out.payload, "hello", 5) == 0);
}

static void test_limits_and_fuzz(TestStats *stats)
{
    uint8_t payload[REVERSI_NET_WS_MAX_PAYLOAD + 1];
    uint8_t frame[4096];
    memset(payload, 'x', sizeof(payload));
    REVERSI_TEST_ASSERT(stats, net_ws_encode(
        0x1, payload, REVERSI_NET_WS_MAX_PAYLOAD + 1, false, NULL, frame,
        sizeof(frame)) == 0);
    REVERSI_TEST_ASSERT(stats, net_ws_encode(
        0x9, payload, 126, false, NULL, frame, sizeof(frame)) == 0);

    uint64_t rng = 0x1234;
    NetWsParser parser;
    NetWsFrame out;
    for (int i = 0; i < 20000; ++i) {
        uint8_t data[64];
        const size_t length = (size_t)(rng % sizeof(data));
        for (size_t j = 0; j < length; ++j) {
            rng = rng * 6364136223846793005ULL + 1;
            data[j] = (uint8_t)(rng >> 33);
        }
        net_ws_parser_init(&parser);
        size_t consumed = 0;
        (void)net_ws_parse(&parser, data, length, true, &out, &consumed);
        REVERSI_TEST_ASSERT(stats, consumed <= length);
    }
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_accept_key);
    REVERSI_RUN_TEST(&stats, test_frames);
    REVERSI_RUN_TEST(&stats, test_fragmentation);
    REVERSI_RUN_TEST(&stats, test_limits_and_fuzz);
    return reversi_test_summary(&stats, "net.ws");
}
