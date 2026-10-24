#include "analysis/analysis.h"
#include "analysis/record.h"
#include "core/game.h"
#include "core/history.h"
#include "core/rules.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Move nth_legal(const GameState *state, int index)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &state->board, state->current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    return index >= 0 && index < count ? moves[index] : (Move){-1, -1};
}

static void play_random_plies(AnalysisSession *session, int plies,
                              uint64_t *rng)
{
    for (int i = 0; i < plies; ++i) {
        const AnalysisPosition *pos = analysis_current(session);
        if (pos == NULL || game_is_over(&pos->state)) {
            return;
        }
        if (game_must_pass(&pos->state)) {
            if (!analysis_pass(session)) {
                return;
            }
            continue;
        }
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &pos->state.board, pos->state.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        if (count <= 0) {
            return;
        }
        *rng ^= *rng >> 12;
        *rng ^= *rng << 25;
        *rng ^= *rng >> 27;
        const uint64_t value = *rng * 0x2545F4914F6CDD1DULL;
        analysis_play_move(session, moves[value % (uint64_t)count]);
    }
}

static void test_tree_navigation(TestStats *stats)
{
    AnalysisSession session;
    analysis_init(&session);
    REVERSI_TEST_ASSERT(stats, session.count == 1);
    REVERSI_TEST_ASSERT(stats, analysis_current_kind(&session)
                        == ANALYSIS_POSITION_VALID);
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->ply == 0);

    const Move d3 = {5, 3};
    REVERSI_TEST_ASSERT(stats, analysis_play_move(&session, d3));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->ply == 1);
    REVERSI_TEST_ASSERT(stats, analysis_prev(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->ply == 0);
    REVERSI_TEST_ASSERT(stats, analysis_next(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->ply == 1);
    REVERSI_TEST_ASSERT(stats, analysis_prev(&session));

    /* Replaying the same move navigates instead of duplicating. */
    REVERSI_TEST_ASSERT(stats, analysis_play_move(&session, d3));
    REVERSI_TEST_ASSERT(stats, session.count == 2);

    /* A different move at the root creates a sibling branch. */
    REVERSI_TEST_ASSERT(stats, analysis_prev(&session));
    const Move c4 = {4, 2};
    REVERSI_TEST_ASSERT(stats, analysis_play_move(&session, c4));
    REVERSI_TEST_ASSERT(stats, session.count == 3);
    REVERSI_TEST_ASSERT(stats, analysis_variation_count(&session) == 0);
    REVERSI_TEST_ASSERT(stats, analysis_prev(&session));
    REVERSI_TEST_ASSERT(stats, analysis_variation_count(&session) == 2);

    AnalysisVariation variation;
    REVERSI_TEST_ASSERT(stats, analysis_variation_at(&session, 0, &variation));
    REVERSI_TEST_ASSERT(stats, variation.length == 1);
    REVERSI_TEST_ASSERT(stats, variation.child_index == 0);

    /* Move to the second branch and back to the main line. */
    REVERSI_TEST_ASSERT(stats, analysis_next(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->via.move.row
                        == d3.row);
    REVERSI_TEST_ASSERT(stats, analysis_next_variation(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->via.move.row
                        == c4.row);
    REVERSI_TEST_ASSERT(stats, analysis_return_to_main(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->via.move.row
                        == d3.row);
}

static void test_illegal_and_editing(TestStats *stats)
{
    AnalysisSession session;
    analysis_init(&session);
    const Move occupied = {3, 3};
    REVERSI_TEST_ASSERT(stats, !analysis_play_move(&session, occupied));
    REVERSI_TEST_ASSERT(stats, session.count == 1);

    /* Editing turns the current board into a custom root and drops the tree. */
    REVERSI_TEST_ASSERT(stats, analysis_play_move(&session, (Move){5, 3}));
    REVERSI_TEST_ASSERT(stats, session.count == 2);
    REVERSI_TEST_ASSERT(stats, analysis_edit_cell(&session, 0, 0,
                                                 STONE_BLACK));
    REVERSI_TEST_ASSERT(stats, session.count == 1);
    REVERSI_TEST_ASSERT(stats, analysis_current_kind(&session)
                        == ANALYSIS_POSITION_CUSTOM);
    REVERSI_TEST_ASSERT(stats, board_get(&analysis_current(&session)->state.board,
                                         0, 0) == STONE_BLACK);

    REVERSI_TEST_ASSERT(stats, analysis_clear_board(&session));
    REVERSI_TEST_ASSERT(stats, board_count(&analysis_current(&session)->state.board,
                                           STONE_BLACK) == 0);
    REVERSI_TEST_ASSERT(stats, !analysis_current_playable(&session));

    REVERSI_TEST_ASSERT(stats, analysis_set_standard(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current_kind(&session)
                        == ANALYSIS_POSITION_VALID);
    REVERSI_TEST_ASSERT(stats, analysis_current_playable(&session));

    REVERSI_TEST_ASSERT(stats, analysis_edit_cell(&session, 7, 7,
                                                 STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, analysis_set_turn(&session, STONE_WHITE));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->state.current_player
                        == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, analysis_swap_colors(&session));
    REVERSI_TEST_ASSERT(stats, board_get(&analysis_current(&session)->state.board,
                                         7, 7) == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->state.current_player
                        == STONE_BLACK);
}

static void test_pass_node(TestStats *stats)
{
    /* White a1, Black a2: Black has no move, White can play a3 and flip a2. */
    GameState state;
    memset(&state, 0, sizeof(state));
    board_clear(&state.board);
    board_set(&state.board, 7, 0, STONE_WHITE);
    board_set(&state.board, 6, 0, STONE_BLACK);
    state.current_player = STONE_BLACK;
    state.result = GAME_RESULT_ONGOING;

    AnalysisSession session;
    analysis_init(&session);
    REVERSI_TEST_ASSERT(stats, analysis_load_state(
        &session, &state, ANALYSIS_POSITION_CUSTOM));
    REVERSI_TEST_ASSERT(stats, game_must_pass(&state));
    REVERSI_TEST_ASSERT(stats, analysis_current_playable(&session));
    REVERSI_TEST_ASSERT(stats, analysis_pass(&session));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->via.is_pass);
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->state.current_player
                        == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, !analysis_pass(&session));
}

static void test_history_import(TestStats *stats)
{
    GameState state;
    game_init(&state);
    History history;
    history_init(&history, &state);

    uint64_t rng = 0x12345678ULL;
    for (int ply = 0; ply < 12; ++ply) {
        if (game_is_over(&state)) {
            break;
        }
        if (game_must_pass(&state)) {
            GameState passed = state;
            if (!game_pass(&passed)) {
                break;
            }
            state = passed;
            continue;
        }
        Move moves[REVERSI_BOARD_CELL_COUNT];
        const int count = rules_generate_legal_moves(
            &state.board, state.current_player, moves,
            REVERSI_BOARD_CELL_COUNT);
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        const uint64_t value = rng * 0x2545F4914F6CDD1DULL;
        const Move move = moves[value % (uint64_t)count];
        const Stone mover = state.current_player;
        Board before = state.board;
        const int flips = rules_apply_move(&before, mover, move);
        GameState next = state;
        if (!game_apply_move(&next, move)) {
            break;
        }
        PassEvent pass = {0};
        if (next.result == GAME_RESULT_ONGOING
            && next.current_player == mover) {
            pass.occurred = true;
            pass.player = stone_opponent(mover);
        }
        if (!history_record(&history, &next, move, mover, flips, pass)) {
            break;
        }
        state = next;
    }

    AnalysisSession session;
    REVERSI_TEST_ASSERT(stats, analysis_import_history(&session, &history));
    REVERSI_TEST_ASSERT(stats, analysis_current(&session)->ply
                        == history.count - 1);
    REVERSI_TEST_ASSERT(stats, game_state_equal(
        &analysis_current(&session)->state,
        &history.entries[history.count - 1].state));
}

static void test_record_roundtrip(TestStats *stats)
{
    AnalysisSession session;
    analysis_init(&session);
    uint64_t rng = 0xDEADBEEFULL;
    play_random_plies(&session, 6, &rng);

    /* Add a second branch at the root. */
    const int main_end = session.current;
    REVERSI_TEST_ASSERT(stats, analysis_first(&session));
    const Move alternative = nth_legal(&analysis_current(&session)->state, 1);
    REVERSI_TEST_ASSERT(stats, alternative.row >= 0);
    REVERSI_TEST_ASSERT(stats, analysis_play_move(&session, alternative));
    REVERSI_TEST_ASSERT(stats, analysis_goto(&session, main_end));

    char buffer[ANALYSIS_MAX_RECORD_BYTES];
    const size_t written = analysis_record_write(&session, buffer,
                                                 sizeof(buffer));
    REVERSI_TEST_ASSERT(stats, written > 0);
    if (written == 0) {
        return;
    }
    REVERSI_TEST_ASSERT(stats, strncmp(buffer, "reversi-record 1\n", 17) == 0);
    REVERSI_TEST_ASSERT(stats, strstr(buffer, "variation 0 ") != NULL);

    AnalysisSession loaded;
    char error[128] = {0};
    REVERSI_TEST_ASSERT(stats, analysis_record_read(&loaded, buffer, error,
                                                    sizeof(error)));
    REVERSI_TEST_ASSERT(stats, analysis_current(&loaded)->ply
                        == session.positions[session.current].ply);
    REVERSI_TEST_ASSERT(stats, game_state_equal(
        &analysis_current(&loaded)->state,
        &analysis_current(&session)->state));

    REVERSI_TEST_ASSERT(stats, analysis_first(&loaded));
    REVERSI_TEST_ASSERT(stats, analysis_variation_count(&loaded) == 2);

    /* Round trip is stable. */
    char again[ANALYSIS_MAX_RECORD_BYTES];
    const size_t rewritten = analysis_record_write(&loaded, again,
                                                   sizeof(again));
    REVERSI_TEST_ASSERT(stats, rewritten == written);
    REVERSI_TEST_ASSERT(stats, strcmp(buffer, again) == 0);
}

static void test_record_rejects_bad_input(TestStats *stats)
{
    AnalysisSession session;
    analysis_init(&session);
    analysis_play_move(&session, (Move){5, 3});
    const GameState before = analysis_current(&session)->state;

    char error[128];
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session, "not a record\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session, "reversi-record 1\nmoves d3 z9\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session, "reversi-record 1\nmoves d3 d3\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session,
        "reversi-record 1\nmoves d3\nvariation 9 c4\n", error,
        sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session,
        "reversi-record 1\nresult B+99\nmoves d3\n", error, sizeof(error)));
    REVERSI_TEST_ASSERT(stats, !analysis_record_read(
        &session,
        "reversi-record 2\nmoves d3\n", error, sizeof(error)));

    /* Every rejected import left the session untouched. */
    REVERSI_TEST_ASSERT(stats, game_state_equal(
        &analysis_current(&session)->state, &before));
}

int main(void)
{
    TestStats stats = {0};
    REVERSI_RUN_TEST(&stats, test_tree_navigation);
    REVERSI_RUN_TEST(&stats, test_illegal_and_editing);
    REVERSI_RUN_TEST(&stats, test_pass_node);
    REVERSI_RUN_TEST(&stats, test_history_import);
    REVERSI_RUN_TEST(&stats, test_record_roundtrip);
    REVERSI_RUN_TEST(&stats, test_record_rejects_bad_input);
    return reversi_test_summary(&stats, "analysis");
}
