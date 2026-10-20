#include "settings/settings.h"
#include "test_harness.h"
#include "ui/help_view.h"
#include "ui/menu_view.h"
#include "ui/saves_view.h"
#include "ui/settings_view.h"
#include "ui/stats_view.h"
#include "ui/theme.h"

#include <string.h>

static void test_theme_switch(TestStats *stats)
{
    ui_theme_set(REVERSI_UI_THEME_DARK);
    REVERSI_TEST_ASSERT(stats,
        ui_theme_current() == REVERSI_UI_THEME_DARK);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_theme_name(REVERSI_UI_THEME_DARK), "Dark") == 0);
    const Color dark = REVERSI_UI_COLOR_BG_TOP;

    ui_theme_set(REVERSI_UI_THEME_LIGHT);
    REVERSI_TEST_ASSERT(stats,
        ui_theme_current() == REVERSI_UI_THEME_LIGHT);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_theme_name(REVERSI_UI_THEME_LIGHT), "Light") == 0);
    const Color light = REVERSI_UI_COLOR_BG_TOP;
    REVERSI_TEST_ASSERT(stats,
        dark.r != light.r || dark.g != light.g || dark.b != light.b);

    ui_theme_set((UiThemeKind)99);
    REVERSI_TEST_ASSERT(stats,
        ui_theme_current() == REVERSI_UI_THEME_LIGHT);
    ui_theme_set(REVERSI_UI_THEME_DARK);
}

static void test_menu_continue_and_focus(TestStats *stats)
{
    MenuView menu;
    menu_view_init(&menu);
    REVERSI_TEST_ASSERT(stats, menu.focus == 1);

    UiInput input = {0};
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        menu_view_update(&menu, &input, true, 0.016f, false)
            == MENU_ACTION_PLAY);

    menu.focus = 0;
    UiInput down = {0};
    down.down_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        menu_view_update(&menu, &down, true, 0.016f, false)
            == MENU_ACTION_NONE);
    REVERSI_TEST_ASSERT(stats, menu.focus == 1);

    menu.focus = 0;
    REVERSI_TEST_ASSERT(stats,
        menu_view_update(&menu, &input, true, 0.016f, false)
            == MENU_ACTION_NONE);
    REVERSI_TEST_ASSERT(stats,
        menu_view_update(&menu, &input, true, 0.016f, true)
            == MENU_ACTION_CONTINUE);
}

static void test_settings_actions(TestStats *stats)
{
    SettingsView view;
    settings_view_init(&view);
    Settings settings = {
        .volume = 70,
        .sfx = true,
        .animations = true,
        .animation_speed = 100,
        .legal_hints = true,
        .last_move_highlight = true,
        .default_difficulty = 1,
        .theme = 0
    };

    view.focus = 1;
    view.focus_visible = true;
    UiInput right = {0};
    right.right_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &right, true, 0.016f, &settings)
            == SETTINGS_ACTION_SPEED_UP);

    UiInput left = {0};
    left.left_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &left, true, 0.016f, &settings)
            == SETTINGS_ACTION_SPEED_DOWN);

    view.focus = 5;
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &left, true, 0.016f, &settings)
            == SETTINGS_ACTION_VOLUME_DOWN);
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &right, true, 0.016f, &settings)
            == SETTINGS_ACTION_VOLUME_UP);

    view.focus = 0;
    UiInput enter = {0};
    enter.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &enter, true, 0.016f, &settings)
            == SETTINGS_ACTION_TOGGLE_ANIMATIONS);

    UiInput escape = {0};
    escape.escape_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        settings_view_update(&view, &escape, true, 0.016f, &settings)
            == SETTINGS_ACTION_BACK);
}

static void test_saves_actions(TestStats *stats)
{
    SaveSlotInfo slots[REVERSI_SAVES_ROW_COUNT] = {0};
    slots[0] = (SaveSlotInfo){true, true, "autosave"};
    slots[1] = (SaveSlotInfo){true, true, "slot 1"};
    slots[3] = (SaveSlotInfo){true, false, "corrupt"};

    SavesView view;
    saves_view_init(&view);
    view.focus = 1;
    UiInput enter = {0};
    enter.enter_pressed = true;
    SavesAction action = saves_view_update(
        &view, &enter, true, 0.016f, true, slots);
    REVERSI_TEST_ASSERT(stats, action.type == SAVES_ACTION_SAVE);
    REVERSI_TEST_ASSERT(stats, action.row == 1);

    action = saves_view_update(
        &view, &enter, true, 0.016f, false, slots);
    REVERSI_TEST_ASSERT(stats, action.type == SAVES_ACTION_LOAD);
    REVERSI_TEST_ASSERT(stats, action.row == 1);

    view.focus = 3;
    UiInput remove = {0};
    remove.delete_pressed = true;
    action = saves_view_update(
        &view, &remove, true, 0.016f, false, slots);
    REVERSI_TEST_ASSERT(stats, action.type == SAVES_ACTION_DELETE);
    REVERSI_TEST_ASSERT(stats, action.row == 3);

    view.focus = 0;
    action = saves_view_update(
        &view, &remove, true, 0.016f, true, slots);
    REVERSI_TEST_ASSERT(stats, action.type == SAVES_ACTION_NONE);

    UiInput escape = {0};
    escape.escape_pressed = true;
    action = saves_view_update(
        &view, &escape, true, 0.016f, false, slots);
    REVERSI_TEST_ASSERT(stats, action.type == SAVES_ACTION_BACK);
}

static void test_help_and_stats(TestStats *stats)
{
    HelpView help;
    help_view_init(&help);
    REVERSI_TEST_ASSERT(stats, help.page == 0);

    UiInput right = {0};
    right.right_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        help_view_update(&help, &right, true, 0.016f) == HELP_ACTION_NONE);
    REVERSI_TEST_ASSERT(stats, help.page == 1);

    UiInput left = {0};
    left.left_pressed = true;
    (void)help_view_update(&help, &left, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, help.page == 0);

    UiInput escape = {0};
    escape.escape_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        help_view_update(&help, &escape, true, 0.016f) == HELP_ACTION_BACK);

    StatsView stats_view;
    stats_view_init(&stats_view);
    REVERSI_TEST_ASSERT(stats,
        stats_view_update(&stats_view, &escape, true, 0.016f)
            == STATS_ACTION_BACK);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_theme_switch);
    REVERSI_RUN_TEST(&stats, test_menu_continue_and_focus);
    REVERSI_RUN_TEST(&stats, test_settings_actions);
    REVERSI_RUN_TEST(&stats, test_saves_actions);
    REVERSI_RUN_TEST(&stats, test_help_and_stats);

    return reversi_test_summary(&stats, "ui.views");
}
