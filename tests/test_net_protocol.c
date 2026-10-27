#include "net/net_protocol.h"
#include "test_harness.h"

#include <string.h>

static void test_roundtrip(TestStats *stats)
{
    NetMessage hello = {.type = NET_MSG_HELLO, .version = 1};
    char text[REVERSI_NET_MAX_MESSAGE];
    REVERSI_TEST_ASSERT(stats, net_protocol_encode(&hello, text,
                                                   sizeof(text)) > 0);
    NetMessage decoded;
    REVERSI_TEST_ASSERT(stats, net_protocol_decode(
        text, NET_DIRECTION_CLIENT_TO_SERVER, &decoded));
    REVERSI_TEST_ASSERT(stats, decoded.type == NET_MSG_HELLO);
    REVERSI_TEST_ASSERT(stats, decoded.version == 1);

    NetMessage move = {
        .type = NET_MSG_MOVE,
        .seq = 12,
        .last = {3, 4},
        .has_last = true
    };
    REVERSI_TEST_ASSERT(stats, net_protocol_encode(&move, text,
                                                   sizeof(text)) > 0);
    REVERSI_TEST_ASSERT(stats, strstr(text, "cell=e5") != NULL);
    REVERSI_TEST_ASSERT(stats, net_protocol_decode(
        text, NET_DIRECTION_CLIENT_TO_SERVER, &decoded));
    REVERSI_TEST_ASSERT(stats, decoded.seq == 12);
    REVERSI_TEST_ASSERT(stats, decoded.has_last
                        && decoded.last.row == 3 && decoded.last.col == 4);

    GameState state;
    game_init(&state);
    char board[65];
    REVERSI_TEST_ASSERT(stats, net_protocol_board_from_state(&state, board));
    NetMessage update = {
        .type = NET_MSG_STATE,
        .turn = STONE_BLACK,
        .seq = 0,
        .result = GAME_RESULT_ONGOING,
        .pass = false,
        .has_last = false
    };
    memcpy(update.board, board, sizeof(board));
    REVERSI_TEST_ASSERT(stats, net_protocol_encode(&update, text,
                                                   sizeof(text)) > 0);
    REVERSI_TEST_ASSERT(stats, net_protocol_decode(
        text, NET_DIRECTION_SERVER_TO_CLIENT, &decoded));
    REVERSI_TEST_ASSERT(stats, strcmp(decoded.board, board) == 0);
    GameState restored;
    REVERSI_TEST_ASSERT(stats, net_protocol_state_from_board(
        decoded.board, decoded.turn, decoded.result, &restored));
    REVERSI_TEST_ASSERT(stats, game_state_equal(&restored, &state));
}

static void test_rejects_bad_input(TestStats *stats)
{
    NetMessage out;
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "bogus", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "hello v=2", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "hello v=1 extra=1", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "move cell=z9 seq=1", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "move cell=e5 seq=-1", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "create color=X", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "welcome v=1 token=abc player=1",
        NET_DIRECTION_SERVER_TO_CLIENT, &out));
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "join code=abc123 color=B", NET_DIRECTION_CLIENT_TO_SERVER, &out));
    /* Direction mismatch is rejected. */
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        "welcome v=1 token=0123456789abcdef0123456789abcdef player=1",
        NET_DIRECTION_CLIENT_TO_SERVER, &out));

    char too_long[REVERSI_NET_MAX_MESSAGE + 8];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    REVERSI_TEST_ASSERT(stats, !net_protocol_decode(
        too_long, NET_DIRECTION_CLIENT_TO_SERVER, &out));
}

static void test_board_validation(TestStats *stats)
{
    GameState state;
    char board[65];
    memset(board, '0', 64);
    board[64] = '\0';
    REVERSI_TEST_ASSERT(stats, !net_protocol_state_from_board(
        board, STONE_BLACK, GAME_RESULT_ONGOING, &state));
    board[0] = '3';
    REVERSI_TEST_ASSERT(stats, !net_protocol_state_from_board(
        board, STONE_BLACK, GAME_RESULT_DRAW, &state));
    board[0] = '0';
    REVERSI_TEST_ASSERT(stats, !net_protocol_state_from_board(
        board, STONE_EMPTY, GAME_RESULT_ONGOING, &state));

    /* An authoritative final may still have moves on the board. */
    GameState live;
    game_init(&live);
    REVERSI_TEST_ASSERT(stats, game_apply_move(&live, (Move){5, 3}));
    char live_board[REVERSI_BOARD_CELL_COUNT + 1];
    REVERSI_TEST_ASSERT(stats, net_protocol_board_from_state(
        &live, live_board));
    GameState terminal;
    REVERSI_TEST_ASSERT(stats, net_protocol_state_from_board(
        live_board, live.current_player, GAME_RESULT_BLACK_WIN, &terminal));
    REVERSI_TEST_ASSERT(stats, terminal.result == GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, !net_protocol_state_from_board(
        live_board, live.current_player, (GameResult)99, &terminal));
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_roundtrip);
    REVERSI_RUN_TEST(&stats, test_rejects_bad_input);
    REVERSI_RUN_TEST(&stats, test_board_validation);
    return reversi_test_summary(&stats, "net.protocol");
}
