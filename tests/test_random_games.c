#include "core/game.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_oracle.h"

#include <stdint.h>
#include <stdio.h>

#define REVERSI_RANDOM_GAMES 500
#define REVERSI_MAX_GAME_MOVES 60

static uint32_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return (uint32_t)((x * UINT64_C(0x2545F4914F6CDD1D)) >> 32);
}

static int occupied_count(const Board *board)
{
    return board_count(board, STONE_BLACK)
        + board_count(board, STONE_WHITE);
}

static int play_random_game(TestStats *stats, uint64_t *rng)
{
    GameState state;
    game_init(&state);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));

    int moves_played = 0;
    while (!game_is_over(&state)) {
        if (moves_played > REVERSI_MAX_GAME_MOVES) {
            REVERSI_TEST_ASSERT(stats, false);
            break;
        }

        REVERSI_TEST_ASSERT(stats, board_is_valid(&state.board));
        REVERSI_TEST_ASSERT(stats,
            occupied_count(&state.board) == 4 + moves_played);
        REVERSI_TEST_ASSERT(stats,
            board_count(&state.board, STONE_EMPTY) == 60 - moves_played);

        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &state.board, state.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        REVERSI_TEST_ASSERT(stats, count > 0);
        REVERSI_TEST_ASSERT(stats,
            count == rules_count_legal_moves(&state.board,
                                             state.current_player));
        if (count <= 0) {
            break;
        }

        for (int i = 0; i < count; ++i) {
            REVERSI_TEST_ASSERT(stats,
                rules_is_legal_move(&state.board,
                                    state.current_player, moves[i]));
        }

        const Move move = moves[next_random(rng) % (uint32_t)count];
        const Stone mover = state.current_player;
        REVERSI_TEST_ASSERT(stats,
            oracle_is_legal_move(&state.board, mover, move));
        if (!game_apply_move(&state, move)) {
            REVERSI_TEST_ASSERT(stats, false);
            break;
        }

        ++moves_played;
        REVERSI_TEST_ASSERT(stats,
            board_get(&state.board, move.row, move.col) == mover);
        REVERSI_TEST_ASSERT(stats,
            occupied_count(&state.board) == 4 + moves_played);
        REVERSI_TEST_ASSERT(stats,
            board_count(&state.board, STONE_EMPTY) == 60 - moves_played);
        REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));
    }

    REVERSI_TEST_ASSERT(stats, game_is_over(&state));
    REVERSI_TEST_ASSERT(stats, moves_played <= REVERSI_MAX_GAME_MOVES);
    REVERSI_TEST_ASSERT(stats,
        occupied_count(&state.board) == 4 + moves_played);
    REVERSI_TEST_ASSERT(stats, game_is_consistent(&state));

    const int black = board_count(&state.board, STONE_BLACK);
    const int white = board_count(&state.board, STONE_WHITE);
    GameResult expected = GAME_RESULT_DRAW;
    if (black > white) {
        expected = GAME_RESULT_BLACK_WIN;
    } else if (white > black) {
        expected = GAME_RESULT_WHITE_WIN;
    }
    REVERSI_TEST_ASSERT(stats, state.result == expected);

    return moves_played;
}

static void test_random_games(TestStats *stats)
{
    uint64_t rng = UINT64_C(0x9E3779B97F4A7C15);
    int total_moves = 0;
    int max_moves = 0;
    int finished = 0;

    for (int game = 0; game < REVERSI_RANDOM_GAMES; ++game) {
        const int moves = play_random_game(stats, &rng);
        total_moves += moves;
        if (moves > max_moves) {
            max_moves = moves;
        }
        ++finished;
    }

    printf("random games: %d finished, total moves %d, max moves %d\n",
           finished, total_moves, max_moves);
    REVERSI_TEST_ASSERT(stats, finished == REVERSI_RANDOM_GAMES);
    REVERSI_TEST_ASSERT(stats, max_moves <= REVERSI_MAX_GAME_MOVES);
    REVERSI_TEST_ASSERT(stats, max_moves > 0);
}

int main(void)
{
    TestStats stats = {0, 0};

    REVERSI_RUN_TEST(&stats, test_random_games);
    return reversi_test_summary(&stats, "core.random");
}
