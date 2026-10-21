#include "storage/stats.h"
#include "storage/storage.h"
#include "test_harness.h"

#include <stdio.h>
#include <string.h>

static void test_records(TestStats *stats)
{
    GameStats record;
    stats_reset(&record);
    REVERSI_TEST_ASSERT(stats, stats_total_games(&record) == 0);

    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_WHITE_WIN);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_DRAW);
    stats_record(&record, GAME_MODE_PVE, STONE_WHITE, 2,
                 GAME_RESULT_WHITE_WIN);
    stats_record(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                 GAME_RESULT_DRAW);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 5,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_ONGOING);

    REVERSI_TEST_ASSERT(stats, record.pve[0].games == 3);
    REVERSI_TEST_ASSERT(stats, record.pve[0].wins == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[0].draws == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[0].losses == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[2].games == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[2].wins == 1);
    REVERSI_TEST_ASSERT(stats, record.pvp_games == 2);
    REVERSI_TEST_ASSERT(stats, record.pvp_draws == 1);

    REVERSI_TEST_ASSERT(stats, stats_total_games(&record) == 6);
    REVERSI_TEST_ASSERT(stats, stats_human_games(&record) == 4);
    REVERSI_TEST_ASSERT(stats, stats_human_wins(&record) == 2);
    REVERSI_TEST_ASSERT(stats, stats_human_draws(&record) == 1);
    REVERSI_TEST_ASSERT(stats, stats_human_losses(&record) == 1);
}

static void test_unrecord(TestStats *stats)
{
    GameStats record;
    stats_reset(&record);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVE, STONE_WHITE, 2,
                 GAME_RESULT_WHITE_WIN);
    stats_record(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                 GAME_RESULT_DRAW);

    stats_unrecord(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                   GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, record.pve[0].games == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[0].wins == 1);
    REVERSI_TEST_ASSERT(stats, stats_total_games(&record) == 4);

    stats_unrecord(&record, GAME_MODE_PVE, STONE_WHITE, 2,
                   GAME_RESULT_WHITE_WIN);
    REVERSI_TEST_ASSERT(stats, record.pve[2].games == 0);
    REVERSI_TEST_ASSERT(stats, record.pve[2].wins == 0);

    /* Unrecording a record that is not present must not go negative. */
    stats_unrecord(&record, GAME_MODE_PVE, STONE_WHITE, 2,
                   GAME_RESULT_WHITE_WIN);
    stats_unrecord(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                   GAME_RESULT_DRAW);
    REVERSI_TEST_ASSERT(stats, record.pve[0].games == 1);
    REVERSI_TEST_ASSERT(stats, record.pve[0].draws == 0);

    stats_unrecord(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                   GAME_RESULT_DRAW);
    REVERSI_TEST_ASSERT(stats, record.pvp_games == 1);
    REVERSI_TEST_ASSERT(stats, record.pvp_draws == 0);
    stats_unrecord(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                   GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, record.pvp_games == 0);
    stats_unrecord(&record, GAME_MODE_PVP, STONE_BLACK, 0,
                   GAME_RESULT_BLACK_WIN);
    REVERSI_TEST_ASSERT(stats, record.pvp_games == 0);
    stats_unrecord(&record, GAME_MODE_PVE, STONE_BLACK, 0,
                   GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, record.pve[0].games == 1);
}

static void test_round_trip(TestStats *stats)
{
    const char *path = "test-stats.dat";
    (void)storage_remove(path);

    GameStats saved;
    stats_reset(&saved);
    stats_record(&saved, GAME_MODE_PVE, STONE_BLACK, 1,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&saved, GAME_MODE_PVE, STONE_WHITE, 1,
                 GAME_RESULT_BLACK_WIN);
    stats_record(&saved, GAME_MODE_PVP, STONE_BLACK, 0,
                 GAME_RESULT_DRAW);

    REVERSI_TEST_ASSERT(stats, stats_save_path(path, &saved));
    GameStats loaded;
    stats_reset(&loaded);
    REVERSI_TEST_ASSERT(stats, stats_load_path(path, &loaded));
    REVERSI_TEST_ASSERT(stats, loaded.pve[1].games == 2);
    REVERSI_TEST_ASSERT(stats, loaded.pve[1].wins == 1);
    REVERSI_TEST_ASSERT(stats, loaded.pve[1].losses == 1);
    REVERSI_TEST_ASSERT(stats, loaded.pvp_games == 1);
    REVERSI_TEST_ASSERT(stats, loaded.pvp_draws == 1);

    (void)storage_remove(path);
}

static void test_invalid_and_missing(TestStats *stats)
{
    const char *path = "test-stats-invalid.dat";
    (void)storage_remove(path);
    GameStats record;
    stats_reset(&record);
    record.pve[0].wins = 3;
    REVERSI_TEST_ASSERT(stats, !stats_load_path(path, &record));
    REVERSI_TEST_ASSERT(stats, stats_total_games(&record) == 0);

    const char *bad_sum =
        "REVERSI_STATS 1\npvp_games 1\npvp_draws 2\n"
        "easy_games 1\neasy_wins 0\neasy_draws 0\neasy_losses 0\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_sum, strlen(bad_sum)));
    REVERSI_TEST_ASSERT(stats, !stats_load_path(path, &record));
    REVERSI_TEST_ASSERT(stats, stats_total_games(&record) == 0);

    const char *bad_record =
        "REVERSI_STATS 1\n"
        "easy_games 2\neasy_wins 1\neasy_draws 0\neasy_losses 0\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_record, strlen(bad_record)));
    REVERSI_TEST_ASSERT(stats, !stats_load_path(path, &record));

    const char *bad_header = "REVERSI_STATS 2\npvp_games 1\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_header, strlen(bad_header)));
    REVERSI_TEST_ASSERT(stats, !stats_load_path(path, &record));

    const char *bad_number = "REVERSI_STATS 1\npvp_games -1\n";
    REVERSI_TEST_ASSERT(stats,
        storage_write_atomic(path, bad_number, strlen(bad_number)));
    REVERSI_TEST_ASSERT(stats, !stats_load_path(path, &record));

    (void)storage_remove(path);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_records);
    REVERSI_RUN_TEST(&stats, test_unrecord);
    REVERSI_RUN_TEST(&stats, test_round_trip);
    REVERSI_RUN_TEST(&stats, test_invalid_and_missing);

    return reversi_test_summary(&stats, "storage.stats");
}
