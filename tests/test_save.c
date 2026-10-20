#include "core/game.h"
#include "core/match.h"
#include "core/rules.h"
#include "storage/save.h"
#include "storage/storage.h"
#include "test_harness.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

#define TEST_SAVE_PATH "test-save.rvs"

static bool dummy_select(void *context, const GameState *state,
                         Move *out_move)
{
    (void)context;
    (void)state;
    (void)out_move;
    return false;
}

static MatchConfig pvp_config(void)
{
    return (MatchConfig){
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
}

static MatchConfig pve_config(Stone human)
{
    static const AiController dummy = {
        .name = "dummy",
        .select_move = dummy_select,
        .context = NULL
    };
    return (MatchConfig){
        .mode = GAME_MODE_PVE,
        .human_color = human,
        .ai = &dummy
    };
}

static Move first_legal_move(const Match *match)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &match->state.board, match->state.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    return count > 0 ? moves[0] : (Move){0, 0};
}

static void play_move(TestStats *stats, Match *match, Move move)
{
    REVERSI_TEST_ASSERT(stats, match_apply_move(match, move));
    if (match->phase == MATCH_PHASE_ANIMATING) {
        REVERSI_TEST_ASSERT(stats, match_finish_animation(match));
    }
    if (match->phase == MATCH_PHASE_PASSING_TURN) {
        REVERSI_TEST_ASSERT(stats, match_finish_pass(match));
    }
}

static unsigned long checksum_text(const char *text)
{
    unsigned long hash = 2166136261UL;
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; ++cursor) {
        hash ^= (unsigned long)*cursor;
        hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
    }
    return hash;
}

static void write_custom_checked(TestStats *stats, const char *path,
                                 int version, bool force_checksum,
                                 unsigned long checksum, const char *payload)
{
    char file[2048];
    const unsigned long sum = force_checksum
        ? checksum : checksum_text(payload);
    const int written = snprintf(file, sizeof(file),
                                 "REVERSI_SAVE %d\nchecksum %08lX\n%s",
                                 version, sum, payload);
    REVERSI_TEST_ASSERT(stats, written > 0);
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, file, (size_t)written));
}

static void board_text(const Board *board, char out[65])
{
    int index = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board_get(board, row, col);
            out[index++] = stone == STONE_BLACK ? 'B'
                : stone == STONE_WHITE ? 'W' : '.';
        }
    }
    out[index] = '\0';
}

static void build_initial_payload(char *out, size_t cap)
{
    GameState initial;
    game_init(&initial);
    char board[65];
    board_text(&initial.board, board);
    snprintf(out, cap,
             "mode 0\nhuman 1\ndifficulty 0\nmoves 0\n"
             "board %s\nplayer 1\nresult 0\n", board);
}

static void test_round_trip(TestStats *stats)
{
    (void)storage_remove(TEST_SAVE_PATH);
    Match match;
    const MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    play_move(stats, &match, first_legal_move(&match));
    play_move(stats, &match, first_legal_move(&match));
    play_move(stats, &match, first_legal_move(&match));

    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats,
        save_write(TEST_SAVE_PATH, &match, 1) == SAVE_STATUS_OK);

    SavedGame saved;
    REVERSI_TEST_ASSERT(stats,
        save_read(TEST_SAVE_PATH, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.mode == GAME_MODE_PVP);
    REVERSI_TEST_ASSERT(stats, saved.human_color == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, saved.difficulty == 1);
    REVERSI_TEST_ASSERT(stats,
        saved.move_count == history_position(&match.history));
    REVERSI_TEST_ASSERT(stats,
        game_state_equal(&saved.expected, &match.state));

    for (int index = 1; index <= saved.move_count; ++index) {
        const HistoryEntry *entry = history_entry(&match.history, index);
        REVERSI_TEST_ASSERT(stats, entry != NULL);
        if (entry != NULL) {
            REVERSI_TEST_ASSERT(stats,
                entry->move.row == saved.moves[index - 1].row
                && entry->move.col == saved.moves[index - 1].col);
        }
    }

    Match restored;
    REVERSI_TEST_ASSERT(stats,
        match_restore(&restored, &match.config, saved.moves,
                      saved.move_count));
    REVERSI_TEST_ASSERT(stats,
        game_state_equal(&restored.state, &saved.expected));

    /* Overwrite with a longer match. */
    play_move(stats, &match, first_legal_move(&match));
    REVERSI_TEST_ASSERT(stats,
        save_write(TEST_SAVE_PATH, &match, 2) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats,
        save_read(TEST_SAVE_PATH, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.difficulty == 2);
    REVERSI_TEST_ASSERT(stats,
        saved.move_count == history_position(&match.history));
    REVERSI_TEST_ASSERT(stats,
        game_state_equal(&saved.expected, &match.state));

    (void)storage_remove(TEST_SAVE_PATH);
    (void)storage_remove(TEST_SAVE_PATH ".tmp");
}

static void test_pve_round_trip(TestStats *stats)
{
    const char *path = "test-save-pve.rvs";
    (void)storage_remove(path);
    Match match;
    const MatchConfig config = pve_config(STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    play_move(stats, &match, first_legal_move(&match));
    REVERSI_TEST_ASSERT(stats, save_write(path, &match, 2)
        == SAVE_STATUS_OK);

    SavedGame saved;
    REVERSI_TEST_ASSERT(stats, save_read(path, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.mode == GAME_MODE_PVE);
    REVERSI_TEST_ASSERT(stats, saved.human_color == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, saved.difficulty == 2);

    Match restored;
    REVERSI_TEST_ASSERT(stats,
        match_restore(&restored, &match.config, saved.moves,
                      saved.move_count));
    REVERSI_TEST_ASSERT(stats,
        game_state_equal(&restored.state, &saved.expected));
    REVERSI_TEST_ASSERT(stats, restored.config.mode == GAME_MODE_PVE);

    (void)storage_remove(path);
}

static void test_reject_bad_files(TestStats *stats)
{
    const char *path = "test-save-bad.rvs";
    (void)storage_remove(path);
    SavedGame saved;
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_NOT_FOUND);

    char payload[512];
    build_initial_payload(payload, sizeof(payload));

    write_custom_checked(stats, path, 2, true, 0, payload);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_VERSION);

    write_custom_checked(stats, path, 1, true, 0x12345678UL, payload);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_CHECKSUM);

    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, "REVERSI_SAVE 1\n", 15));
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_FORMAT);

    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, "garbage", 7));
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_FORMAT);

    char board[65];
    GameState initial;
    game_init(&initial);
    board_text(&initial.board, board);

    char illegal_payload[512];
    snprintf(illegal_payload, sizeof(illegal_payload),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 1\nmove 0 0\n"
             "board %s\nplayer 1\nresult 0\n", board);
    write_custom_checked(stats, path, 1, false, 0, illegal_payload);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_STATE);

    char bad_board_text[65];
    memcpy(bad_board_text, board, sizeof(bad_board_text));
    bad_board_text[0] = 'B';
    char bad_board[512];
    snprintf(bad_board, sizeof(bad_board),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 0\n"
             "board %s\nplayer 1\nresult 0\n", bad_board_text);
    write_custom_checked(stats, path, 1, false, 0, bad_board);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_STATE);

    char bad_player[512];
    snprintf(bad_player, sizeof(bad_player),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 0\n"
             "board %s\nplayer 2\nresult 0\n", board);
    write_custom_checked(stats, path, 1, false, 0, bad_player);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_STATE);

    char bad_result[512];
    snprintf(bad_result, sizeof(bad_result),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 0\n"
             "board %s\nplayer 1\nresult 1\n", board);
    write_custom_checked(stats, path, 1, false, 0, bad_result);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_STATE);

    char too_many[512];
    snprintf(too_many, sizeof(too_many),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 65\n"
             "board %s\nplayer 1\nresult 0\n", board);
    write_custom_checked(stats, path, 1, false, 0, too_many);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_FORMAT);

    char bad_coord[512];
    snprintf(bad_coord, sizeof(bad_coord),
             "mode 0\nhuman 1\ndifficulty 0\nmoves 1\nmove 8 0\n"
             "board %s\nplayer 1\nresult 0\n", board);
    write_custom_checked(stats, path, 1, false, 0, bad_coord);
    REVERSI_TEST_ASSERT(stats,
        save_read(path, &saved) == SAVE_STATUS_FORMAT);

    (void)storage_remove(path);
}

static void test_atomic_overwrite(TestStats *stats)
{
    const char *path = "test-save-atomic.rvs";
    char temp[64];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    (void)storage_remove(path);
    (void)storage_remove(temp);
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(temp, "partial garbage", 15));

    char payload[512];
    build_initial_payload(payload, sizeof(payload));
    write_custom_checked(stats, path, 1, false, 0, payload);

    SavedGame saved;
    REVERSI_TEST_ASSERT(stats, save_read(path, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.move_count == 0);

    (void)storage_remove(path);
    (void)storage_remove(temp);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_round_trip);
    REVERSI_RUN_TEST(&stats, test_pve_round_trip);
    REVERSI_RUN_TEST(&stats, test_reject_bad_files);
    REVERSI_RUN_TEST(&stats, test_atomic_overwrite);

    return reversi_test_summary(&stats, "storage.save");
}
