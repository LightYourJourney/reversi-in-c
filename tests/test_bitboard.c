#include "core/bitboard.h"
#include "core/board.h"
#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t legal_mask_from_rules(const Board *board, Stone player)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(board, player, moves,
                                                 REVERSI_BOARD_CELL_COUNT);
    uint64_t mask = 0;
    for (int i = 0; i < count; ++i) {
        mask |= 1ULL << bitboard_index(moves[i].row, moves[i].col);
    }
    return mask;
}

static uint64_t mirror_horizontal(uint64_t bits)
{
    uint64_t mirrored = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const unsigned index = bitboard_index(row, col);
            if ((bits & (1ULL << index)) != 0) {
                mirrored |= 1ULL << bitboard_index(row,
                                                   REVERSI_BOARD_SIZE - 1 - col);
            }
        }
    }
    return mirrored;
}

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static void check_position(TestStats *stats, const GameState *state)
{
    Bitboard bb;
    bitboard_from_board(&state->board, &bb);

    REVERSI_TEST_ASSERT(stats, bitboard_valid(&bb));
    REVERSI_TEST_ASSERT(stats,
        bitboard_count(&bb, STONE_BLACK)
            + bitboard_count(&bb, STONE_WHITE) == 64
            - bitboard_popcount(bitboard_empty(&bb)));
    REVERSI_TEST_ASSERT(stats,
        bitboard_popcount(bitboard_empty(&bb))
            == 64 - bitboard_count(&bb, STONE_BLACK)
                   - bitboard_count(&bb, STONE_WHITE));

    Board roundtrip;
    REVERSI_TEST_ASSERT(stats, bitboard_to_board(&bb, &roundtrip));
    REVERSI_TEST_ASSERT(stats, boards_equal(&state->board, &roundtrip));

    REVERSI_TEST_ASSERT(stats,
        bitboard_must_pass(&bb, state->current_player)
            == rules_must_pass(&state->board, state->current_player));

    const uint64_t rules_mask =
        legal_mask_from_rules(&state->board, state->current_player);
    const uint64_t bb_mask =
        bitboard_legal_moves(&bb, state->current_player);
    REVERSI_TEST_ASSERT(stats, rules_mask == bb_mask);

    const bool terminal = legal_mask_from_rules(&state->board, STONE_BLACK) == 0
        && legal_mask_from_rules(&state->board, STONE_WHITE) == 0;
    REVERSI_TEST_ASSERT(stats, terminal == bitboard_is_terminal(&bb));

    for (int index = 0; index < REVERSI_BOARD_CELL_COUNT; ++index) {
        if ((bb_mask & (1ULL << index)) == 0) {
            continue;
        }
        const Move move = {index / REVERSI_BOARD_SIZE,
                           index % REVERSI_BOARD_SIZE};

        Board scalar = state->board;
        const int scalar_flips =
            rules_apply_move(&scalar, state->current_player, move);
        REVERSI_TEST_ASSERT(stats, scalar_flips >= 1);

        Bitboard child = bb;
        uint64_t flips = 0;
        REVERSI_TEST_ASSERT(stats,
            bitboard_apply_move(&child, state->current_player, index, &flips));
        REVERSI_TEST_ASSERT(stats,
            bitboard_popcount(flips) == (unsigned)scalar_flips);

        Board bb_board;
        REVERSI_TEST_ASSERT(stats, bitboard_to_board(&child, &bb_board));
        REVERSI_TEST_ASSERT(stats, boards_equal(&scalar, &bb_board));
    }

    /* Edge symmetry: a horizontal mirror preserves legality under mirroring. */
    const Bitboard mirrored = { mirror_horizontal(bb.black),
                                mirror_horizontal(bb.white) };
    REVERSI_TEST_ASSERT(stats,
        bitboard_legal_moves(&mirrored, state->current_player)
            == mirror_horizontal(bb_mask));
}

static void test_roundtrip_and_counts(TestStats *stats)
{
    GameState state;
    game_init(&state);
    Bitboard bb;
    bitboard_from_board(&state.board, &bb);
    REVERSI_TEST_ASSERT(stats, bb.black ==
        ((1ULL << bitboard_index(3, 3)) | (1ULL << bitboard_index(4, 4))));
    REVERSI_TEST_ASSERT(stats, bb.white ==
        ((1ULL << bitboard_index(3, 4)) | (1ULL << bitboard_index(4, 3))));
    REVERSI_TEST_ASSERT(stats, bitboard_count(&bb, STONE_BLACK) == 2);
    REVERSI_TEST_ASSERT(stats, bitboard_count(&bb, STONE_WHITE) == 2);
    REVERSI_TEST_ASSERT(stats, bitboard_disc_diff(&bb, STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats, bitboard_popcount(bitboard_empty(&bb)) == 60);
}

static void test_edge_cases_do_not_wrap(TestStats *stats)
{
    static const struct {
        int black_row, black_col;
        int white_row, white_col;
        int expect_row, expect_col;
    } cases[] = {
        {0, 7, 0, 6, 0, 5},   /* h8 -> g8 brackets, lands f8, no wrap */
        {0, 0, 0, 1, 0, 2},   /* a8 -> b8, lands c8 */
        {7, 7, 7, 6, 7, 5},   /* h1 -> g1, lands f1 */
        {7, 0, 7, 1, 7, 2},   /* a1 -> b1, lands c1 */
        {0, 7, 1, 7, 2, 7},   /* h8 -> h7, lands h6 */
        {7, 0, 6, 0, 5, 0}    /* a1 -> a2, lands a3 */
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Bitboard bb = {0, 0};
        bb.black = 1ULL << bitboard_index(cases[i].black_row,
                                          cases[i].black_col);
        bb.white = 1ULL << bitboard_index(cases[i].white_row,
                                          cases[i].white_col);
        const uint64_t legal = bitboard_legal_moves(&bb, STONE_BLACK);
        const unsigned expected = bitboard_index(cases[i].expect_row,
                                                 cases[i].expect_col);
        REVERSI_TEST_ASSERT(stats, (legal & (1ULL << expected)) != 0);
        REVERSI_TEST_ASSERT(stats, bitboard_popcount(legal) == 1);

        Board board;
        REVERSI_TEST_ASSERT(stats, bitboard_to_board(&bb, &board));
        REVERSI_TEST_ASSERT(stats,
            legal_mask_from_rules(&board, STONE_BLACK) == legal);
    }
}

static void test_invalid_input(TestStats *stats)
{
    const Bitboard overlap = {1, 1};
    REVERSI_TEST_ASSERT(stats, !bitboard_valid(&overlap));
    REVERSI_TEST_ASSERT(stats,
        bitboard_legal_moves(&overlap, STONE_BLACK) == 0);
    Board board;
    REVERSI_TEST_ASSERT(stats, !bitboard_to_board(&overlap, &board));

    GameState state;
    game_init(&state);
    state.board.cells[3][3] = STONE_BLACK;
    state.board.cells[3][4] = STONE_BLACK;
    Bitboard start;
    bitboard_from_board(&state.board, &start);
    Bitboard bb = start;
    REVERSI_TEST_ASSERT(stats,
        bitboard_flips(&bb, STONE_BLACK, -1) == 0);
    REVERSI_TEST_ASSERT(stats,
        bitboard_flips(&bb, STONE_BLACK, REVERSI_BOARD_CELL_COUNT) == 0);
    REVERSI_TEST_ASSERT(stats,
        !bitboard_apply_move(&bb, STONE_BLACK, -1, NULL));
    REVERSI_TEST_ASSERT(stats,
        !bitboard_apply_move(&bb, STONE_BLACK, 0, NULL));
    REVERSI_TEST_ASSERT(stats, bb.black == start.black
        && bb.white == start.white);
}

static void test_random_games(TestStats *stats)
{
    uint64_t rng = 0xB17B0A2D5EED1234ULL;
    for (int game = 0; game < 200; ++game) {
        GameState state;
        game_init(&state);
        for (int ply = 0; ply < 200; ++ply) {
            check_position(stats, &state);
            Move moves[REVERSI_BOARD_CELL_COUNT];
            const int count = rules_generate_legal_moves(
                &state.board, state.current_player, moves,
                REVERSI_BOARD_CELL_COUNT);
            if (count <= 0) {
                break;
            }
            const Move move = moves[next_random(&rng) % (uint64_t)count];
            if (!game_apply_move(&state, move)) {
                REVERSI_TEST_ASSERT(stats, false);
                break;
            }
            if (state.result != GAME_RESULT_ONGOING) {
                check_position(stats, &state);
                break;
            }
        }
    }
}

int main(void)
{
    TestStats stats = {0, 0};
    REVERSI_RUN_TEST(&stats, test_roundtrip_and_counts);
    REVERSI_RUN_TEST(&stats, test_edge_cases_do_not_wrap);
    REVERSI_RUN_TEST(&stats, test_invalid_input);
    REVERSI_RUN_TEST(&stats, test_random_games);
    return reversi_test_summary(&stats, "core.bitboard");
}
