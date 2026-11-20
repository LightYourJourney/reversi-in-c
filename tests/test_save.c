#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* setenv and getcwd for the non-ASCII user directory test. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "core/game.h"
#include "core/match.h"
#include "core/rules.h"
#include "storage/save.h"
#include "storage/storage.h"
#include "test_harness.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

#define REVERSI_TEST_SAVE_PATH "test-save.rvs"

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
    (void)storage_remove(REVERSI_TEST_SAVE_PATH);
    Match match;
    const MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    play_move(stats, &match, first_legal_move(&match));
    play_move(stats, &match, first_legal_move(&match));
    play_move(stats, &match, first_legal_move(&match));

    REVERSI_TEST_ASSERT(stats, match_is_consistent(&match));
    REVERSI_TEST_ASSERT(stats,
        save_write(REVERSI_TEST_SAVE_PATH, &match, 1) == SAVE_STATUS_OK);

    SavedGame saved;
    REVERSI_TEST_ASSERT(stats,
        save_read(REVERSI_TEST_SAVE_PATH, &saved) == SAVE_STATUS_OK);
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
        save_write(REVERSI_TEST_SAVE_PATH, &match, 2) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats,
        save_read(REVERSI_TEST_SAVE_PATH, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.difficulty == 2);
    REVERSI_TEST_ASSERT(stats,
        saved.move_count == history_position(&match.history));
    REVERSI_TEST_ASSERT(stats,
        game_state_equal(&saved.expected, &match.state));

    (void)storage_remove(REVERSI_TEST_SAVE_PATH);
    (void)storage_remove(REVERSI_TEST_SAVE_PATH ".tmp");
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

static void test_autosave_action(TestStats *stats)
{
    Match match;
    const MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    REVERSI_TEST_ASSERT(stats, save_autosave_action(&match, false)
        == SAVE_AUTOSAVE_WRITE);
    /* Online play never touches the local autosave, finished or not. */
    REVERSI_TEST_ASSERT(stats, save_autosave_action(&match, true)
        == SAVE_AUTOSAVE_SKIP);
    match.phase = MATCH_PHASE_GAME_OVER;
    REVERSI_TEST_ASSERT(stats, save_autosave_action(&match, false)
        == SAVE_AUTOSAVE_REMOVE);
    REVERSI_TEST_ASSERT(stats, save_autosave_action(&match, true)
        == SAVE_AUTOSAVE_SKIP);
    REVERSI_TEST_ASSERT(stats, save_autosave_action(NULL, false)
        == SAVE_AUTOSAVE_SKIP);
}

static void test_refuse_custom_root(TestStats *stats)
{
    /* A save is replayed from the standard opening, so a match that began
     * elsewhere (a mid-game position, an online game) must be refused
     * rather than written as a file that can never be loaded. */
    const char *path = "test-save-custom.rvs";
    (void)storage_remove(path);
    Match source;
    const MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_start(&source, &config));
    play_move(stats, &source, first_legal_move(&source));
    play_move(stats, &source, first_legal_move(&source));

    Match custom;
    REVERSI_TEST_ASSERT(stats, match_start_from(&custom, &config,
                                                &source.state));
    play_move(stats, &custom, first_legal_move(&custom));
    REVERSI_TEST_ASSERT(stats, save_write(path, &custom, 0)
        == SAVE_STATUS_STATE);
    REVERSI_TEST_ASSERT(stats, !storage_exists(path));
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

static void test_status_messages(TestStats *stats)
{
    const SaveStatus statuses[] = {
        SAVE_STATUS_OK,
        SAVE_STATUS_NOT_FOUND,
        SAVE_STATUS_VERSION,
        SAVE_STATUS_CHECKSUM,
        SAVE_STATUS_STATE,
        SAVE_STATUS_FORMAT,
        SAVE_STATUS_IO
    };
    for (size_t index = 0; index < sizeof(statuses) / sizeof(statuses[0]);
         ++index) {
        const char *message = save_status_message(statuses[index]);
        REVERSI_TEST_ASSERT(stats, message != NULL && message[0] != '\0');
    }
    REVERSI_TEST_ASSERT(stats,
        save_status_message((SaveStatus)999) != NULL);
}

static void test_slot_summary(TestStats *stats)
{
    char text[80];
    SavedGame saved = {0};
    saved.mode = GAME_MODE_PVE;
    saved.difficulty = 1;
    saved.move_count = 12;
    saved.expected.result = GAME_RESULT_ONGOING;
    save_slot_summary(&saved, "Medium", text, sizeof(text));
    REVERSI_TEST_ASSERT(stats, strcmp(text, "PvE / Medium / 12 moves") == 0);

    saved.expected.result = GAME_RESULT_BLACK_WIN;
    save_slot_summary(&saved, "Medium", text, sizeof(text));
    REVERSI_TEST_ASSERT(stats,
        strcmp(text, "PvE / Medium / 12 moves / finished") == 0);

    saved.mode = GAME_MODE_PVP;
    saved.move_count = 5;
    saved.expected.result = GAME_RESULT_ONGOING;
    save_slot_summary(&saved, NULL, text, sizeof(text));
    REVERSI_TEST_ASSERT(stats, strcmp(text, "PvP / 5 moves") == 0);

    /* A missing name still yields a readable line. */
    saved.mode = GAME_MODE_PVE;
    save_slot_summary(&saved, NULL, text, sizeof(text));
    REVERSI_TEST_ASSERT(stats, strcmp(text, "PvE / ? / 5 moves") == 0);

    text[0] = 'x';
    save_slot_summary(NULL, "Medium", text, sizeof(text));
    REVERSI_TEST_ASSERT(stats, text[0] == 'x');
    save_slot_summary(&saved, "Medium", NULL, sizeof(text));
    save_slot_summary(&saved, "Medium", text, 0);
    REVERSI_TEST_ASSERT(stats, text[0] == 'x');
}

static void test_storage_path(TestStats *stats)
{
    char path[1024];
    REVERSI_TEST_ASSERT(stats, !storage_path(NULL, path, sizeof(path)));
    REVERSI_TEST_ASSERT(stats, !storage_path("", path, sizeof(path)));
    REVERSI_TEST_ASSERT(stats, !storage_path(".", path, sizeof(path)));
    REVERSI_TEST_ASSERT(stats, !storage_path("..", path, sizeof(path)));
    REVERSI_TEST_ASSERT(stats, !storage_path("a/b", path, sizeof(path)));
    REVERSI_TEST_ASSERT(stats, !storage_path("save.rvs", path, 1));

    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        REVERSI_TEST_ASSERT(stats,
            storage_path("save.rvs", path, sizeof(path)));
        REVERSI_TEST_ASSERT(stats, strstr(path, "save.rvs") != NULL);
    }
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

static void test_v101_fixture(TestStats *stats)
{
    /* Verbatim save_write() output from tag v1.0.1 (compiled from that
     * tag's src/storage/save.c). Loading it here is the cross-version
     * fixture for published releases. */
    static const char LEGACY[] =
        "REVERSI_SAVE 1\n"
        "checksum CBA0BAD2\n"
        "mode 0\n"
        "human 1\n"
        "difficulty 0\n"
        "moves 1\n"
        "move 2 4\n"
        "board ....................B......BB......WB...........................\n"
        "player 2\n"
        "result 0\n";
    const char *path = "test-save-v101.rvs";
    (void)storage_remove(path);
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, LEGACY, strlen(LEGACY)));

    SavedGame saved;
    REVERSI_TEST_ASSERT(stats, save_read(path, &saved) == SAVE_STATUS_OK);
    REVERSI_TEST_ASSERT(stats, saved.mode == GAME_MODE_PVP);
    REVERSI_TEST_ASSERT(stats, saved.human_color == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, saved.difficulty == 0);
    REVERSI_TEST_ASSERT(stats, saved.move_count == 1);
    REVERSI_TEST_ASSERT(stats, saved.moves[0].row == 2
                        && saved.moves[0].col == 4);
    REVERSI_TEST_ASSERT(stats,
                        saved.expected.current_player == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats,
                        saved.expected.result == GAME_RESULT_ONGOING);

    Match restored;
    const MatchConfig config = pvp_config();
    REVERSI_TEST_ASSERT(stats, match_restore(&restored, &config, saved.moves,
                                             saved.move_count));
    REVERSI_TEST_ASSERT(stats,
                        game_state_equal(&restored.state, &saved.expected));
    (void)storage_remove(path);
}

/*
 * The user directory lives under a profile path the player does not choose;
 * on Windows it often contains characters outside the active code page
 * (C:\Users\Zoë, C:\Users\张三). Points the platform's base directory at
 * such a path and round-trips a file through every storage entry point.
 */
static void test_non_ascii_user_dir(TestStats *stats)
{
    /* "Zoë-张三" */
#ifdef _WIN32
    const wchar_t *variable = L"APPDATA";
    wchar_t *saved = NULL;
    const wchar_t *old = _wgetenv(variable);
    if (old != NULL) {
        saved = _wcsdup(old);
    }
    wchar_t base[600];
    REVERSI_TEST_ASSERT(stats, _wgetcwd(base, 500) != NULL);
    wcscat_s(base, 600, L"\\unicode-Zo\u00EB-\u5F20\u4E09");
    REVERSI_TEST_ASSERT(stats, _wputenv_s(variable, base) == 0);
#else
    char *saved_home = getenv("HOME") != NULL ? strdup(getenv("HOME")) : NULL;
    char *saved_xdg = getenv("XDG_DATA_HOME") != NULL
        ? strdup(getenv("XDG_DATA_HOME")) : NULL;
    char base[1100];
    REVERSI_TEST_ASSERT(stats, getcwd(base, 1000) != NULL);
    strcat(base, "/unicode-Zo\xC3\xAB-\xE5\xBC\xA0\xE4\xB8\x89");
    REVERSI_TEST_ASSERT(stats, setenv("HOME", base, 1) == 0);
    REVERSI_TEST_ASSERT(stats, setenv("XDG_DATA_HOME", base, 1) == 0);
#endif

    char path[1024];
    REVERSI_TEST_ASSERT(stats, storage_make_user_dir());
    REVERSI_TEST_ASSERT(stats, storage_path("unicode.rvs", path,
                                            sizeof(path)));
    REVERSI_TEST_ASSERT(stats,
        strstr(path, "Zo\xC3\xAB-\xE5\xBC\xA0\xE4\xB8\x89") != NULL);
    (void)storage_remove(path);
    REVERSI_TEST_ASSERT(stats, storage_write_atomic(path, "one", 3));
    REVERSI_TEST_ASSERT(stats, storage_write_atomic(path, "two!", 4));
    REVERSI_TEST_ASSERT(stats, storage_exists(path));
    char *data = NULL;
    size_t size = 0;
    REVERSI_TEST_ASSERT(stats, storage_read(path, &data, &size, 64));
    REVERSI_TEST_ASSERT(stats, size == 4 && data != NULL
                                   && memcmp(data, "two!", 4) == 0);
    free(data);
    REVERSI_TEST_ASSERT(stats, storage_remove(path));
    REVERSI_TEST_ASSERT(stats, !storage_exists(path));

#ifdef _WIN32
    (void)_wputenv_s(variable, saved != NULL ? saved : L"");
    free(saved);
#else
    if (saved_home != NULL) {
        (void)setenv("HOME", saved_home, 1);
    } else {
        (void)unsetenv("HOME");
    }
    if (saved_xdg != NULL) {
        (void)setenv("XDG_DATA_HOME", saved_xdg, 1);
    } else {
        (void)unsetenv("XDG_DATA_HOME");
    }
    free(saved_home);
    free(saved_xdg);
#endif
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_round_trip);
    REVERSI_RUN_TEST(&stats, test_pve_round_trip);
    REVERSI_RUN_TEST(&stats, test_autosave_action);
    REVERSI_RUN_TEST(&stats, test_refuse_custom_root);
    REVERSI_RUN_TEST(&stats, test_reject_bad_files);
    REVERSI_RUN_TEST(&stats, test_status_messages);
    REVERSI_RUN_TEST(&stats, test_slot_summary);
    REVERSI_RUN_TEST(&stats, test_storage_path);
    REVERSI_RUN_TEST(&stats, test_atomic_overwrite);
    REVERSI_RUN_TEST(&stats, test_v101_fixture);
    REVERSI_RUN_TEST(&stats, test_non_ascii_user_dir);

    return reversi_test_summary(&stats, "storage.save");
}
