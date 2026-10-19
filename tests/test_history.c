#include "core/game.h"
#include "core/history.h"
#include "core/rules.h"
#include "test_harness.h"
#include "test_helpers.h"

static PassEvent no_pass(void)
{
    return (PassEvent){false, STONE_EMPTY};
}

static int move_flips(const GameState *before, const GameState *after,
                      Stone mover, Move move)
{
    const Stone opponent = stone_opponent(mover);
    int flips = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (row == move.row && col == move.col) {
                continue;
            }
            if (board_get(&before->board, row, col) == opponent
                && board_get(&after->board, row, col) == mover) {
                ++flips;
            }
        }
    }
    return flips;
}

static void record_move(TestStats *stats, History *history, GameState *state,
                        Move move)
{
    const GameState before = *state;
    const Stone mover = state->current_player;
    REVERSI_TEST_ASSERT(stats, game_apply_move(state, move));
    const int flips = move_flips(&before, state, mover, move);
    REVERSI_TEST_ASSERT(stats, history_record(
        history, state, move, mover, flips, no_pass()));
}

static void test_init_and_empty(TestStats *stats)
{
    GameState initial;
    game_init(&initial);

    History history;
    history_init(&history, &initial);

    REVERSI_TEST_ASSERT(stats, history_count(&history) == 1);
    REVERSI_TEST_ASSERT(stats, history_position(&history) == 0);
    REVERSI_TEST_ASSERT(stats, !history_can_undo(&history));
    REVERSI_TEST_ASSERT(stats, !history_can_redo(&history));
    REVERSI_TEST_ASSERT(stats, !history_undo(&history));
    REVERSI_TEST_ASSERT(stats, !history_redo(&history));

    const HistoryEntry *entry = history_current(&history);
    REVERSI_TEST_ASSERT(stats, entry != NULL);
    REVERSI_TEST_ASSERT(stats, entry != NULL && !entry->has_move);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && game_states_equal(&entry->state, &initial));
    REVERSI_TEST_ASSERT(stats, history_entry(&history, -1) == NULL);
    REVERSI_TEST_ASSERT(stats, history_entry(&history, 1) == NULL);
}

static void test_undo_redo_restore(TestStats *stats)
{
    GameState initial;
    game_init(&initial);
    History history;
    history_init(&history, &initial);

    GameState state = initial;
    record_move(stats, &history, &state, (Move){5, 3}); /* d3 */
    const GameState after_d3 = state;
    record_move(stats, &history, &state, (Move){3, 2}); /* c5 */
    const GameState after_c5 = state;
    const HistoryEntry *entry = NULL;

    REVERSI_TEST_ASSERT(stats, history_count(&history) == 3);
    REVERSI_TEST_ASSERT(stats, history_can_undo(&history));
    REVERSI_TEST_ASSERT(stats, !history_can_redo(&history));

    REVERSI_TEST_ASSERT(stats, history_undo(&history));
    entry = history_current(&history);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && game_states_equal(&entry->state, &after_d3));
    REVERSI_TEST_ASSERT(stats, entry != NULL && entry->has_move);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && entry->move.row == 5 && entry->move.col == 3);
    REVERSI_TEST_ASSERT(stats, entry != NULL && entry->mover == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, entry != NULL && entry->flips == 1);

    REVERSI_TEST_ASSERT(stats, history_undo(&history));
    entry = history_current(&history);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && game_states_equal(&entry->state, &initial));
    REVERSI_TEST_ASSERT(stats, !history_can_undo(&history));
    REVERSI_TEST_ASSERT(stats, history_can_redo(&history));

    REVERSI_TEST_ASSERT(stats, history_redo(&history));
    entry = history_current(&history);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && game_states_equal(&entry->state, &after_d3));

    REVERSI_TEST_ASSERT(stats, history_redo(&history));
    entry = history_current(&history);
    REVERSI_TEST_ASSERT(stats, entry != NULL
                        && game_states_equal(&entry->state, &after_c5));
    REVERSI_TEST_ASSERT(stats, !history_can_redo(&history));
}

static void test_new_move_discards_redo(TestStats *stats)
{
    GameState initial;
    game_init(&initial);
    History history;
    history_init(&history, &initial);

    GameState state = initial;
    record_move(stats, &history, &state, (Move){5, 3}); /* d3 */
    record_move(stats, &history, &state, (Move){3, 2}); /* c5 */
    REVERSI_TEST_ASSERT(stats, history_undo(&history));
    REVERSI_TEST_ASSERT(stats, history_can_redo(&history));

    state = history_current(&history)->state;
    record_move(stats, &history, &state, (Move){5, 4}); /* e3 */
    REVERSI_TEST_ASSERT(stats, history_count(&history) == 3);
    REVERSI_TEST_ASSERT(stats, history_position(&history) == 2);
    REVERSI_TEST_ASSERT(stats, !history_can_redo(&history));
    REVERSI_TEST_ASSERT(stats, history_current(&history)->move.col == 4);
}

static void test_pass_and_terminal(TestStats *stats)
{
    Board board;
    board_clear(&board);
    (void)board_set(&board, 0, 2, STONE_BLACK);
    (void)board_set(&board, 0, 1, STONE_WHITE);
    (void)board_set(&board, 3, 4, STONE_WHITE);
    (void)board_set(&board, 3, 5, STONE_BLACK);
    (void)board_set(&board, 3, 6, STONE_BLACK);
    (void)board_set(&board, 3, 7, STONE_BLACK);

    GameState state;
    state.board = board;
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_ONGOING;

    History history;
    history_init(&history, &state);
    const GameState before = state;

    REVERSI_TEST_ASSERT(stats, game_apply_move(&state, (Move){0, 0}));
    const PassEvent pass = {true, STONE_WHITE};
    REVERSI_TEST_ASSERT(stats, history_record(
        &history, &state, (Move){0, 0}, STONE_BLACK, 1, pass));
    REVERSI_TEST_ASSERT(stats,
                        history_current(&history)->pass.occurred);
    REVERSI_TEST_ASSERT(stats,
                        history_current(&history)->pass.player == STONE_WHITE);

    REVERSI_TEST_ASSERT(stats, history_undo(&history));
    REVERSI_TEST_ASSERT(stats, game_states_equal(
        &history_current(&history)->state, &before));
    REVERSI_TEST_ASSERT(stats, !history_current(&history)->pass.occurred);

    REVERSI_TEST_ASSERT(stats, history_redo(&history));
    REVERSI_TEST_ASSERT(stats,
                        history_current(&history)->pass.occurred);
    REVERSI_TEST_ASSERT(stats, history_current(&history)->state.current_player
                        == STONE_BLACK);
}

static void test_capacity_and_invalid(TestStats *stats)
{
    GameState initial;
    game_init(&initial);

    History history;
    history_init(&history, &initial);
    REVERSI_TEST_ASSERT(stats, !history_record(
        NULL, &initial, (Move){0, 0}, STONE_BLACK, 0, no_pass()));
    REVERSI_TEST_ASSERT(stats, !history_record(
        &history, NULL, (Move){0, 0}, STONE_BLACK, 0, no_pass()));

    for (int i = 0; i < REVERSI_HISTORY_MAX_MOVES; ++i) {
        REVERSI_TEST_ASSERT(stats, history_record(
            &history, &initial, (Move){0, 0}, STONE_BLACK, 0, no_pass()));
    }
    REVERSI_TEST_ASSERT(stats, history_count(&history)
                        == REVERSI_HISTORY_MAX_ENTRIES);
    REVERSI_TEST_ASSERT(stats, !history_record(
        &history, &initial, (Move){0, 0}, STONE_BLACK, 0, no_pass()));
    REVERSI_TEST_ASSERT(stats, history_count(&history)
                        == REVERSI_HISTORY_MAX_ENTRIES);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_init_and_empty);
    REVERSI_RUN_TEST(&stats, test_undo_redo_restore);
    REVERSI_RUN_TEST(&stats, test_new_move_discards_redo);
    REVERSI_RUN_TEST(&stats, test_pass_and_terminal);
    REVERSI_RUN_TEST(&stats, test_capacity_and_invalid);

    return reversi_test_summary(&stats, "core.history");
}
