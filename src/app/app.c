#include "app/app.h"

#include "ai/ai.h"
#include "ai/ai_worker.h"
#include "audio/audio.h"
#include "core/game.h"
#include "core/match.h"
#include "core/rules.h"
#include "reversi/version.h"
#include "settings/settings.h"
#include "storage/save.h"
#include "storage/stats.h"
#include "storage/storage.h"
#include "ui/animation.h"
#include "ui/draw.h"
#include "ui/fonts.h"
#include "ui/game_view.h"
#include "ui/help_view.h"
#include "ui/history_view.h"
#include "ui/lab_view.h"
#include "ui/net_view.h"
#include "net/net_session.h"
#include "ui/menu_view.h"
#include "ui/modal.h"
#include "ui/pause_view.h"
#include "ui/result_view.h"
#include "ui/saves_view.h"
#include "ui/settings_view.h"
#include "ui/setup_view.h"
#include "ui/stats_view.h"
#include "ui/theme.h"

#include <raylib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REVERSI_WINDOW_WIDTH 1100
#define REVERSI_WINDOW_HEIGHT 800
#define REVERSI_TARGET_FPS 60
#define REVERSI_AUTOSAVE_FILE "autosave.rvs"

typedef enum AppScreen {
    APP_SCREEN_MENU = 0,
    APP_SCREEN_SETUP,
    APP_SCREEN_GAME,
    APP_SCREEN_HISTORY,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_RESULT,
    APP_SCREEN_HELP,
    APP_SCREEN_STATS,
    APP_SCREEN_SAVES,
    APP_SCREEN_LAB,
    APP_SCREEN_NET
} AppScreen;

typedef enum PendingGameAction {
    PENDING_GAME_NONE = 0,
    PENDING_GAME_START,
    PENDING_GAME_LOAD,
    PENDING_GAME_UNDO
} PendingGameAction;

typedef struct AppTransition {
    bool active;
    bool switched;
    float t;
    float duration;
    AppScreen target;
} AppTransition;

typedef struct App {
    AppScreen screen;
    Settings settings;
    GameStats stats;
    UiFonts fonts;
    MenuView menu;
    SetupView setup_view;
    GameView game_view;
    PauseView pause_view;
    HistoryView history_view;
    SettingsView settings_view;
    ResultView result_view;
    HelpView help_view;
    StatsView stats_view;
    SavesView saves_view;
    LabView lab;
    NetView net_view;
    NetSession *net;
    bool net_mode;
    int net_state_seq;
    bool net_move_pending;
    UiButton net_resign;
    UiButton net_leave;
    SaveSlotInfo save_slots[REVERSI_SAVES_ROW_COUNT];
    bool saves_saving;
    AppScreen saves_return;
    bool has_autosave;
    bool autosave_error_shown;
    char notice[160];
    float notice_timer;
    Modal modal;
    AppTransition transition;
    Match match;
    UiMoveAnimation animation;
    float last_move_age;
    float outcome_delay;
    float pass_notice_timer;
    bool running;
    bool autoplay;
    float autoplay_timer;
    PendingGameAction pending_action;
    MatchConfig pending_config;
    const AiController *ai;
    AiWorker *ai_worker;
    AiDifficulty ai_difficulty;
    unsigned ai_seed;
    bool stats_recorded;
    int pending_delete_row;
} App;

static UiInput read_input(void);
static void app_init(App *app);
static void app_reset_match_ui(App *app);
static void app_begin_match(App *app, const MatchConfig *config);
static void app_request_screen(App *app, AppScreen target);
static void app_update_transition(App *app, float dt);
static void app_update_passive(App *app, float dt);
static void app_update(App *app, const UiInput *input, float dt);
static void app_update_game(App *app, const UiInput *input, float dt);
static void app_draw(const App *app);
static void app_draw_transition(const App *app);
static void app_finish_animation(App *app);
static void app_apply_move(App *app, Move move);
static void app_undo(App *app);
static void app_redo(App *app);
static void app_after_history_change(App *app);
static void app_handle_menu_action(App *app, MenuAction action);
static void app_handle_pause_action(App *app, PauseAction action);
static void app_handle_modal_confirm(App *app);
static void app_cancel_ai(App *app);
static bool app_start_ai(App *app);
static bool app_poll_ai(App *app);
static void app_maybe_start_ai(App *app);
static AiDifficulty app_ai_difficulty_from_setup(SetupDifficulty difficulty);
static void app_autosave(App *app);
static void app_notice(App *app, const char *text);
static void app_update_notice(App *app, float dt);
static void app_draw_notice(const App *app);
static float app_motion_dt(const App *app, float dt);
static bool app_save_path(int row, char *out, size_t cap);
static void app_fill_slot(SaveSlotInfo *slot, const char *path);
static void app_refresh_slots(App *app);
static bool app_restore_saved(App *app, const char *path);
static void app_open_saves(App *app, bool saving);
static void app_handle_settings_action(App *app, SettingsAction action);
static void app_handle_saves_action(App *app, SavesAction action);
static void app_maybe_record_result(App *app);
static void app_play_result_sound(const App *app);
static void app_handle_net_action(App *app, NetViewAction action);
static void app_build_net_match(App *app);
static void app_update_net_game(App *app, const UiInput *input, float dt);
static void app_draw_net_hud(const App *app);

static UiInput read_input(void)
{
    UiInput input = {0};
    input.mouse = GetMousePosition();
    input.mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    input.mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    input.mouse_released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    input.escape_pressed = IsKeyPressed(KEY_ESCAPE);
    input.enter_pressed = IsKeyPressed(KEY_ENTER)
        || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    input.up_pressed = IsKeyPressed(KEY_UP);
    input.down_pressed = IsKeyPressed(KEY_DOWN);
    input.left_pressed = IsKeyPressed(KEY_LEFT);
    input.right_pressed = IsKeyPressed(KEY_RIGHT);
    input.restart_pressed = IsKeyPressed(KEY_R);
    input.menu_pressed = IsKeyPressed(KEY_M);
    input.pause_pressed = IsKeyPressed(KEY_P);
    input.history_pressed = IsKeyPressed(KEY_H);
    input.delete_pressed = IsKeyPressed(KEY_DELETE)
        || IsKeyPressed(KEY_BACKSPACE);
    input.undo_pressed = IsKeyPressed(KEY_U);
    input.redo_pressed = IsKeyPressed(KEY_Y);
    input.wheel = GetMouseWheelMove();

    const bool modifier = IsKeyDown(KEY_LEFT_CONTROL)
        || IsKeyDown(KEY_RIGHT_CONTROL)
        || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT)
        || IsKeyDown(KEY_RIGHT_SHIFT);
    if (modifier && IsKeyPressed(KEY_Z)) {
        if (shift) {
            input.redo_pressed = true;
        } else {
            input.undo_pressed = true;
        }
    }
    if (modifier && IsKeyPressed(KEY_Y)) {
        input.redo_pressed = true;
    }
    if (modifier && IsKeyPressed(KEY_S)) {
        input.save_pressed = true;
    }
    return input;
}

static float app_motion_dt(const App *app, float dt)
{
    const float scale = (float)app->settings.animation_speed / 100.0f;
    return dt * (scale > 0.05f ? scale : 0.05f);
}

static void app_notice(App *app, const char *text)
{
    if (app == NULL || text == NULL) {
        return;
    }
    snprintf(app->notice, sizeof(app->notice), "%s", text);
    app->notice_timer = REVERSI_UI_DUR_NOTICE;
}

static void app_update_notice(App *app, float dt)
{
    if (app->notice_timer > 0.0f) {
        app->notice_timer -= dt;
        if (app->notice_timer < 0.0f) {
            app->notice_timer = 0.0f;
        }
    }
}

static void app_draw_notice(const App *app)
{
    if (app->notice_timer <= 0.0f || app->notice[0] == '\0') {
        return;
    }
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float alpha = app->notice_timer < 0.4f
        ? app->notice_timer / REVERSI_UI_DUR_NOTICE_FADE : 1.0f;
    const float box_width = fminf(560.0f, width - 64.0f);
    const Rectangle box = {
        (width - box_width) * 0.5f, height - 96.0f, box_width, 44.0f
    };
    ui_draw_panel(box, REVERSI_UI_RADIUS_SM,
                  ui_color_alpha(REVERSI_UI_COLOR_SURFACE, 0.97f * alpha),
                  ui_color_alpha(REVERSI_UI_COLOR_BORDER_STRONG, alpha));
    ui_draw_text_clipped(
        &app->fonts, UI_TEXT_REGULAR, app->notice,
        (Rectangle){box.x + 16.0f, box.y, box.width - 32.0f, box.height},
        REVERSI_UI_FONT_SMALL,
        ui_color_alpha(REVERSI_UI_COLOR_TEXT, alpha), UI_ALIGN_LEFT);
}

static void app_init(App *app)
{
    app->screen = APP_SCREEN_MENU;
    settings_defaults(&app->settings);
    (void)settings_load(&app->settings);
    ui_theme_set((UiThemeKind)app->settings.theme);
    stats_reset(&app->stats);
    (void)stats_load(&app->stats);

    app->transition = (AppTransition){0};
    app->modal = (Modal){0};
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->pass_notice_timer = 0.0f;
    app->running = true;
    app->autoplay_timer = 0.0f;
    app->pending_action = PENDING_GAME_NONE;
    app->pending_config = (MatchConfig){
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    app->ai = ai_controller_for_difficulty(AI_DIFFICULTY_MEDIUM);
    app->ai_worker = NULL;
    app->ai_difficulty = AI_DIFFICULTY_MEDIUM;
    app->ai_seed = 0x51ED270Bu;
    app->stats_recorded = true;
    app->pending_delete_row = -1;
    app->saves_saving = false;
    app->saves_return = APP_SCREEN_MENU;
    app->has_autosave = false;
    app->autosave_error_shown = false;
    app->notice[0] = '\0';
    app->notice_timer = 0.0f;
    memset(app->save_slots, 0, sizeof(app->save_slots));

#ifndef NDEBUG
    const char *autoplay = getenv("REVERSI_AUTOPLAY");
    app->autoplay = autoplay != NULL && autoplay[0] != '\0'
        && strcmp(autoplay, "0") != 0;
    if (app->autoplay) {
        app->settings.animations = false;
        app->stats_recorded = false;
        const unsigned long seed = strtoul(autoplay, NULL, 10);
        srand(seed != 0 ? (unsigned)seed : (unsigned)time(NULL));
    }
#else
    app->autoplay = false;
#endif

    ui_fonts_load(&app->fonts);
    app->ai_worker = ai_worker_create();
    menu_view_init(&app->menu);
    setup_view_init(&app->setup_view);
    setup_view_set_ai_available(&app->setup_view, app->ai != NULL);
    setup_view_set_difficulty(
        &app->setup_view, (SetupDifficulty)app->settings.default_difficulty);
    game_view_init(&app->game_view);
    pause_view_init(&app->pause_view);
    history_view_init(&app->history_view);
    settings_view_init(&app->settings_view);
    result_view_init(&app->result_view);
    help_view_init(&app->help_view);
    stats_view_init(&app->stats_view);
    saves_view_init(&app->saves_view);
    lab_view_init(&app->lab);
    net_view_init(&app->net_view);
    app->net = net_session_create();
    app->net_mode = false;
    app->net_state_seq = -1;
    app->net_move_pending = false;
    ui_button_init(&app->net_resign, "Resign", false);
    ui_button_init(&app->net_leave, "Leave", false);
    modal_init(&app->modal);
    ui_animation_reset(&app->animation);

    (void)audio_init(app->settings.volume, app->settings.sfx);

    MatchConfig default_config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    (void)match_start(&app->match, &default_config);

#ifndef NDEBUG
    /* Debug-only render smoke for the analysis screen. */
    const char *lab_smoke = getenv("REVERSI_LAB_SMOKE");
    if (lab_smoke != NULL && lab_smoke[0] != '\0'
        && strcmp(lab_smoke, "0") != 0) {
        lab_view_enter(&app->lab, &app->match, false);
        app->screen = APP_SCREEN_LAB;
    }
    const char *net_smoke = getenv("REVERSI_NET_SMOKE");
    if (net_smoke != NULL && net_smoke[0] != '\0'
        && strcmp(net_smoke, "0") != 0) {
        app->screen = APP_SCREEN_NET;
    }
#endif

    app_refresh_slots(app);

    if (app->autoplay) {
        app->screen = APP_SCREEN_GAME;
    }
}

static void app_reset_match_ui(App *app)
{
    ui_animation_reset(&app->animation);
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->pass_notice_timer = 0.0f;
    app->autoplay_timer = 0.0f;
    game_view_reset(&app->game_view);
    pause_view_reset(&app->pause_view);
}

static AiDifficulty app_ai_difficulty_from_setup(SetupDifficulty difficulty)
{
    switch (difficulty) {
    case SETUP_DIFFICULTY_EASY:
        return AI_DIFFICULTY_EASY;
    case SETUP_DIFFICULTY_HARD:
        return AI_DIFFICULTY_HARD;
    case SETUP_DIFFICULTY_MEDIUM:
    default:
        return AI_DIFFICULTY_MEDIUM;
    }
}

/* Idempotent: actions cancel immediately when they leave the AI turn, and
   the screen/run cleanup paths may cancel again. */
static void app_cancel_ai(App *app)
{
    if (app->ai_worker != NULL) {
        ai_worker_join(app->ai_worker);
    }
    if (app->match.phase == MATCH_PHASE_AI_THINKING) {
        (void)match_cancel_ai_turn(&app->match);
    }
}

static bool app_apply_first_legal(App *app)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = rules_generate_legal_moves(
        &app->match.state.board, app->match.state.current_player, moves,
        REVERSI_BOARD_CELL_COUNT);
    if (count <= 0) {
        return false;
    }
    app_apply_move(app, moves[0]);
    return app->match.phase != MATCH_PHASE_AI_THINKING;
}

static bool app_finish_ai_result(App *app, bool found, Move move)
{
    Match *match = &app->match;
    if (found
        && rules_is_legal_move(&match->state.board,
                               match->state.current_player, move)) {
        app_apply_move(app, move);
        if (match->phase != MATCH_PHASE_AI_THINKING) {
            return true;
        }
    }
    if (app_apply_first_legal(app)) {
        return true;
    }

    (void)match_cancel_ai_turn(match);
    return false;
}

static bool app_start_ai(App *app)
{
    Match *match = &app->match;
    if (app->ai == NULL || match->phase != MATCH_PHASE_PLAYER_TURN
        || match->config.mode != GAME_MODE_PVE
        || match_is_human_turn(match)) {
        return false;
    }
    if (!match_begin_ai_turn(match)) {
        return false;
    }

    AiConfig config = ai_default_config(app->ai_difficulty, app->ai_seed++);
    if (app->ai_worker != NULL
        && ai_worker_start(app->ai_worker, &match->state, &config)) {
        return true;
    }

#ifdef __EMSCRIPTEN__
    /* Never run a blocking search on the browser main thread. */
    app_notice(app, "Computer search unavailable; played a legal move.");
    return app_apply_first_legal(app);
#else
    Move move = {0, 0};
    const bool found = ai_choose_move(&match->state, &config, &move, NULL);
    return app_finish_ai_result(app, found, move);
#endif
}

static bool app_poll_ai(App *app)
{
    Match *match = &app->match;
    if (match->phase != MATCH_PHASE_AI_THINKING) {
        return false;
    }
    const AiWorkerStatus status = app->ai_worker != NULL
        ? ai_worker_status(app->ai_worker) : AI_WORKER_IDLE;
    if (status == AI_WORKER_RUNNING) {
        return false;
    }

    Move move = {0, 0};
    const bool found = app->ai_worker != NULL
        && ai_worker_take(app->ai_worker, &move, NULL);
#ifdef __EMSCRIPTEN__
    if (status == AI_WORKER_FAILED) {
        app_notice(app, "Computer search failed; played a legal move.");
    }
#endif
    (void)app_finish_ai_result(app, found, move);
    return true;
}

static void app_maybe_start_ai(App *app)
{
    if (app->match.phase == MATCH_PHASE_PLAYER_TURN
        && app->match.config.mode == GAME_MODE_PVE
        && !match_is_human_turn(&app->match)) {
        (void)app_start_ai(app);
    }
}

static bool app_save_path(int row, char *out, size_t cap)
{
    if (row == 0) {
        return storage_path(REVERSI_AUTOSAVE_FILE, out, cap);
    }
    char name[32];
    const int written = snprintf(
        name, sizeof(name), "save%d.rvs", row);
    if (written <= 0 || (size_t)written >= sizeof(name)) {
        return false;
    }
    return storage_path(name, out, cap);
}

static void app_fill_slot(SaveSlotInfo *slot, const char *path)
{
    *slot = (SaveSlotInfo){0};
    SavedGame saved;
    const SaveStatus status = save_read(path, &saved);
    if (status == SAVE_STATUS_NOT_FOUND) {
        return;
    }

    slot->present = true;
    if (status != SAVE_STATUS_OK) {
        slot->valid = false;
        snprintf(slot->summary, sizeof(slot->summary), "Unreadable save");
        return;
    }

    slot->valid = true;
    if (saved.mode == GAME_MODE_PVE) {
        snprintf(slot->summary, sizeof(slot->summary),
                 "PvE / %s / %d moves%s",
                 ai_difficulty_name((AiDifficulty)saved.difficulty),
                 saved.move_count,
                 saved.expected.result == GAME_RESULT_ONGOING
                    ? "" : " / finished");
    } else {
        snprintf(slot->summary, sizeof(slot->summary),
                 "PvP / %d moves%s", saved.move_count,
                 saved.expected.result == GAME_RESULT_ONGOING
                    ? "" : " / finished");
    }
}

static void app_refresh_slots(App *app)
{
    for (int row = 0; row < REVERSI_SAVES_ROW_COUNT; ++row) {
        char path[1024];
        if (app_save_path(row, path, sizeof(path))) {
            app_fill_slot(&app->save_slots[row], path);
        } else {
            app->save_slots[row] = (SaveSlotInfo){0};
        }
    }
    app->has_autosave = app->save_slots[0].present
        && app->save_slots[0].valid;
}

static void app_autosave(App *app)
{
    char path[1024];
    if (!storage_make_user_dir()
        || !app_save_path(0, path, sizeof(path))) {
        return;
    }
    if (app->match.phase == MATCH_PHASE_GAME_OVER) {
        (void)storage_remove(path);
        app->has_autosave = false;
        return;
    }

    const SaveStatus status = save_write(
        path, &app->match, (int)app->ai_difficulty);
    if (status == SAVE_STATUS_OK) {
        app->has_autosave = true;
        app->autosave_error_shown = false;
    } else if (!app->autosave_error_shown) {
        app->autosave_error_shown = true;
        app_notice(app, "Autosave failed. Use Save game from the pause menu.");
    }
}

static void app_play_result_sound(const App *app)
{
    const GameResult result = app->match.state.result;
    if (result == GAME_RESULT_DRAW) {
        audio_play(AUDIO_CUE_DRAW);
        return;
    }
    if (result == GAME_RESULT_ONGOING) {
        return;
    }
    if (app->match.config.mode == GAME_MODE_PVE) {
        const GameResult human_win =
            app->match.config.human_color == STONE_BLACK
                ? GAME_RESULT_BLACK_WIN : GAME_RESULT_WHITE_WIN;
        audio_play(result == human_win ? AUDIO_CUE_WIN : AUDIO_CUE_LOSE);
    } else {
        audio_play(AUDIO_CUE_WIN);
    }
}

static void app_save_stats(App *app)
{
    if (!stats_save(&app->stats)) {
        app_notice(app, "Statistics could not be saved.");
    }
}

static void app_unrecord_result(App *app, GameResult previous)
{
    if (!app->stats_recorded || previous == GAME_RESULT_ONGOING) {
        return;
    }
    app->stats_recorded = false;
    stats_unrecord(&app->stats, app->match.config.mode,
                   app->match.config.human_color, (int)app->ai_difficulty,
                   previous);
    app_save_stats(app);
}

static void app_maybe_record_result(App *app)
{
    if (app->match.state.result == GAME_RESULT_ONGOING
        || app->stats_recorded) {
        return;
    }
    app->stats_recorded = true;
    stats_record(&app->stats, app->match.config.mode,
                 app->match.config.human_color, (int)app->ai_difficulty,
                 app->match.state.result);
    app_save_stats(app);
}

static void app_complete_game_over(App *app)
{
    app_autosave(app);
    app_maybe_record_result(app);
    app_play_result_sound(app);
}

static bool app_restore_saved(App *app, const char *path)
{
    SavedGame saved;
    const SaveStatus status = save_read(path, &saved);
    if (status != SAVE_STATUS_OK) {
        app_notice(app, save_status_message(status));
        return false;
    }

    const AiDifficulty difficulty = (AiDifficulty)saved.difficulty;
    const AiController *ai = saved.mode == GAME_MODE_PVE
        ? ai_controller_for_difficulty(difficulty) : NULL;
    if (saved.mode == GAME_MODE_PVE && ai == NULL) {
        app_notice(app, "Save data is inconsistent");
        return false;
    }

    const MatchConfig config = {
        .mode = saved.mode,
        .human_color = saved.human_color,
        .ai = ai
    };
    if (!match_restore_checked(&app->match, &config, saved.moves,
                               saved.move_count, &saved.expected)) {
        app_notice(app, "Save data is inconsistent");
        return false;
    }

    app_cancel_ai(app);
    app->ai = ai;
    app->ai_difficulty = difficulty;
    app->stats_recorded = app->match.phase == MATCH_PHASE_GAME_OVER;
    app->pending_action = PENDING_GAME_LOAD;
    app_notice(app, "Game loaded");
    app_request_screen(app, APP_SCREEN_GAME);
    return true;
}

static void app_open_saves(App *app, bool saving)
{
    if (saving && app->screen != APP_SCREEN_GAME
        && app->screen != APP_SCREEN_RESULT) {
        return;
    }
    app->saves_saving = saving;
    app->saves_return = app->screen == APP_SCREEN_GAME
        || app->screen == APP_SCREEN_RESULT
        ? app->screen : APP_SCREEN_MENU;
    if (saving) {
        app->saves_view.focus = 1;
        app->saves_view.focus_visible = false;
    }
    app_refresh_slots(app);
    app_request_screen(app, APP_SCREEN_SAVES);
}

static void app_switch_screen(App *app, AppScreen target)
{
    if (target != APP_SCREEN_GAME) {
        app_cancel_ai(app);
    }

    app->screen = target;
    if (target != APP_SCREEN_GAME) {
        return;
    }

    switch (app->pending_action) {
    case PENDING_GAME_START:
        if (match_start(&app->match, &app->pending_config)) {
            app_reset_match_ui(app);
            app->stats_recorded = false;
            app_maybe_start_ai(app);
            app_autosave(app);
        } else {
            app->screen = APP_SCREEN_SETUP;
        }
        break;
    case PENDING_GAME_LOAD:
        app_reset_match_ui(app);
        app->outcome_delay = app->match.phase == MATCH_PHASE_GAME_OVER
            ? REVERSI_UI_DUR_GAME_OVER_FAST : 0.0f;
        if (app->match.phase == MATCH_PHASE_GAME_OVER) {
            app_play_result_sound(app);
        }
        app_maybe_start_ai(app);
        app_autosave(app);
        break;
    case PENDING_GAME_UNDO: {
        const GameResult previous = app->match.state.result;
        if (match_undo(&app->match)) {
            app_unrecord_result(app, previous);
            app_reset_match_ui(app);
            app_autosave(app);
        } else {
            app->screen = APP_SCREEN_RESULT;
        }
        break;
    }
    case PENDING_GAME_NONE:
    default:
        break;
    }
    app->pending_action = PENDING_GAME_NONE;
}

static void app_begin_match(App *app, const MatchConfig *config)
{
    if (config == NULL) {
        return;
    }

    app_cancel_ai(app);

    if (app->screen == APP_SCREEN_GAME && !app->transition.active) {
        if (match_start(&app->match, config)) {
            app_reset_match_ui(app);
            app->stats_recorded = false;
            app_maybe_start_ai(app);
            app_autosave(app);
        }
        return;
    }

    app->pending_config = *config;
    app->pending_action = PENDING_GAME_START;
    app->stats_recorded = false;
    app_request_screen(app, APP_SCREEN_GAME);
}

static void app_request_screen(App *app, AppScreen target)
{
    if (app->screen == target && !app->transition.active) {
        return;
    }
    if (app->transition.active) {
        app->transition.target = target;
        return;
    }

    if (!app->settings.animations) {
        app->transition.active = false;
        app->transition.target = target;
        app_switch_screen(app, target);
        app_update_passive(app, 0.0f);
        return;
    }

    app->transition.active = true;
    app->transition.switched = false;
    app->transition.t = 0.0f;
    app->transition.duration = REVERSI_UI_DUR_TRANSITION;
    app->transition.target = target;
}

static void app_update_transition(App *app, float dt)
{
    AppTransition *transition = &app->transition;
    if (!transition->active) {
        return;
    }

    transition->t += dt / transition->duration;
    if (!transition->switched && transition->t >= 0.5f) {
        transition->switched = true;
        app_switch_screen(app, transition->target);
        app_update_passive(app, dt);
    }
    if (transition->t >= 1.0f) {
        transition->t = 1.0f;
        transition->active = false;
    }
}

static void app_update_passive(App *app, float dt)
{
    const UiInput input = {.reduce_motion = !app->settings.animations};
    if (app->net != NULL) {
        net_session_update(app->net, dt);
    }

    switch (app->screen) {
    case APP_SCREEN_MENU:
        (void)menu_view_update(&app->menu, &input, false, dt,
                               app->has_autosave);
        break;
    case APP_SCREEN_SETUP:
        (void)setup_view_update(&app->setup_view, &input, false, dt);
        break;
    case APP_SCREEN_GAME:
        if (app->match.has_last_move) {
            app->last_move_age += dt;
        }
        (void)game_view_update(&app->game_view, &app->match, &input,
                               false, dt);
        if (app->match.phase == MATCH_PHASE_PAUSED) {
            (void)pause_view_update(&app->pause_view, &input, false, dt);
        }
        break;
    case APP_SCREEN_HISTORY:
        (void)history_view_update(&app->history_view, &app->match, &input,
                                  false, dt);
        break;
    case APP_SCREEN_SETTINGS:
        (void)settings_view_update(
            &app->settings_view, &input, false, dt, &app->settings);
        break;
    case APP_SCREEN_RESULT:
        (void)result_view_update(&app->result_view, &input, false,
                                 app->settings.animations,
                                 match_can_undo(&app->match),
                                 app_motion_dt(app, dt));
        break;
    case APP_SCREEN_HELP:
        (void)help_view_update(&app->help_view, &input, false, dt);
        break;
    case APP_SCREEN_STATS:
        (void)stats_view_update(&app->stats_view, &input, false, dt);
        break;
    case APP_SCREEN_SAVES:
        (void)saves_view_update(
            &app->saves_view, &input, false, dt, app->saves_saving,
            app->save_slots);
        break;
    case APP_SCREEN_LAB:
        (void)lab_view_update(
            &app->lab, &input, dt, app->ai_worker, &app->match,
            app->match.history.count > 1);
        break;
    case APP_SCREEN_NET:
        (void)net_view_update(&app->net_view, &input, dt, app->net);
        break;
    }
}

static void app_finish_animation(App *app)
{
    if (!match_finish_animation(&app->match)) {
        return;
    }

    switch (app->match.phase) {
    case MATCH_PHASE_PASSING_TURN:
        app->pass_notice_timer = app->autoplay
            ? 0.0f : REVERSI_UI_DUR_PASS_NOTICE;
        break;
    case MATCH_PHASE_GAME_OVER:
        app->outcome_delay = app->settings.animations
            ? REVERSI_UI_DUR_GAME_OVER : REVERSI_UI_DUR_GAME_OVER_FAST;
        app_complete_game_over(app);
        break;
    default:
        break;
    }
}

static void app_apply_move(App *app, Move move)
{
    const Board before = app->match.state.board;
    const Stone mover = app->match.state.current_player;

    if (!match_apply_move(&app->match, move)) {
        return;
    }

    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->pass_notice_timer = 0.0f;
    audio_play(AUDIO_CUE_PLACE);
    if (app->match.last_flips > 0) {
        audio_play(AUDIO_CUE_FLIP);
    }
    app_maybe_record_result(app);
    app_autosave(app);
    ui_animation_start(&app->animation, &before, &app->match.state.board,
                       move, mover, app->settings.animations);

    if (!ui_animation_is_active(&app->animation)) {
        app_finish_animation(app);
    }
}

static void app_after_history_change(App *app)
{
    ui_animation_reset(&app->animation);
    app->last_move_age = 0.0f;
    app->pass_notice_timer = 0.0f;
    game_view_reset(&app->game_view);
    app->outcome_delay = app->match.phase == MATCH_PHASE_GAME_OVER
        ? (app->settings.animations
            ? REVERSI_UI_DUR_GAME_OVER : REVERSI_UI_DUR_GAME_OVER_FAST)
        : 0.0f;
    if (app->match.phase == MATCH_PHASE_GAME_OVER) {
        app_complete_game_over(app);
    } else {
        app_autosave(app);
    }
}

static void app_undo(App *app)
{
    const GameResult previous = app->match.state.result;
    if (match_undo(&app->match)) {
        app_unrecord_result(app, previous);
        app_after_history_change(app);
    }
}

static void app_redo(App *app)
{
    if (match_redo(&app->match)) {
        app_after_history_change(app);
    }
}

static void app_handle_menu_action(App *app, MenuAction action)
{
    if (action == MENU_ACTION_NONE) {
        return;
    }
    audio_play(AUDIO_CUE_BUTTON);

    switch (action) {
    case MENU_ACTION_CONTINUE: {
        char path[1024];
        if (!storage_path(REVERSI_AUTOSAVE_FILE, path, sizeof(path))
            || !storage_exists(path)) {
            app_notice(app, "No saved game");
            app->has_autosave = false;
            break;
        }
        (void)app_restore_saved(app, path);
        break;
    }
    case MENU_ACTION_LAB:
        lab_view_enter(&app->lab, &app->match,
                       app->match.history.count > 1);
        app_request_screen(app, APP_SCREEN_LAB);
        break;
    case MENU_ACTION_ONLINE:
        app_request_screen(app, APP_SCREEN_NET);
        break;
    case MENU_ACTION_PLAY:
        setup_view_set_difficulty(
            &app->setup_view,
            (SetupDifficulty)app->settings.default_difficulty);
        app_request_screen(app, APP_SCREEN_SETUP);
        break;
    case MENU_ACTION_LOAD:
        app_open_saves(app, false);
        break;
    case MENU_ACTION_HELP:
        help_view_reset(&app->help_view);
        app_request_screen(app, APP_SCREEN_HELP);
        break;
    case MENU_ACTION_STATS:
        app_request_screen(app, APP_SCREEN_STATS);
        break;
    case MENU_ACTION_SETTINGS:
        app_request_screen(app, APP_SCREEN_SETTINGS);
        break;
    case MENU_ACTION_QUIT:
        modal_open(&app->modal, MODAL_PURPOSE_QUIT);
        break;
    case MENU_ACTION_NONE:
    default:
        break;
    }
}

static void app_handle_pause_action(App *app, PauseAction action)
{
    if (action == PAUSE_ACTION_NONE) {
        return;
    }
    audio_play(AUDIO_CUE_BUTTON);

    switch (action) {
    case PAUSE_ACTION_RESUME:
        (void)match_resume(&app->match);
        break;
    case PAUSE_ACTION_SAVE:
        app_open_saves(app, true);
        break;
    case PAUSE_ACTION_HISTORY:
        history_view_open(&app->history_view, &app->match);
        app_request_screen(app, APP_SCREEN_HISTORY);
        break;
    case PAUSE_ACTION_RESTART:
        modal_open(&app->modal, MODAL_PURPOSE_RESTART);
        break;
    case PAUSE_ACTION_MENU:
        modal_open(&app->modal, MODAL_PURPOSE_RETURN_MENU);
        break;
    case PAUSE_ACTION_NONE:
    default:
        break;
    }
}

static void app_handle_modal_confirm(App *app)
{
    const ModalPurpose purpose = app->modal.purpose;
    modal_close(&app->modal);

    switch (purpose) {
    case MODAL_PURPOSE_QUIT:
        if (app->screen == APP_SCREEN_GAME) {
            app_autosave(app);
        }
        app_cancel_ai(app);
        app->running = false;
        break;
    case MODAL_PURPOSE_RESTART:
        app_cancel_ai(app);
        if (match_restart(&app->match)) {
            app_reset_match_ui(app);
            app->stats_recorded = false;
            app_maybe_start_ai(app);
            app_autosave(app);
        }
        break;
    case MODAL_PURPOSE_RETURN_MENU:
        app_autosave(app);
        app_cancel_ai(app);
        app_request_screen(app, APP_SCREEN_MENU);
        break;
    case MODAL_PURPOSE_DELETE_SAVE: {
        char path[1024];
        if (app->pending_delete_row >= 0
            && app_save_path(app->pending_delete_row, path, sizeof(path))) {
            if (storage_remove(path)) {
                app_notice(app, "Save deleted");
            } else {
                app_notice(app, "Could not delete the save");
            }
            app_refresh_slots(app);
        }
        app->pending_delete_row = -1;
        break;
    }
    case MODAL_PURPOSE_NONE:
    default:
        break;
    }
}

static void app_handle_net_action(App *app, NetViewAction action)
{
    if (action == NET_VIEW_NONE || app->net == NULL) {
        return;
    }
    switch (action) {
    case NET_VIEW_CONNECT: {
        const int port = atoi(app->net_view.port);
        if (port <= 0 || port > 65535
            || !net_session_connect(app->net, app->net_view.host, port)) {
            app_notice(app, "Could not start the connection");
        }
        break;
    }
    case NET_VIEW_DISCONNECT:
        net_session_disconnect(app->net);
        break;
    case NET_VIEW_TOGGLE_COLOR:
        app->net_view.color = stone_opponent(app->net_view.color);
        break;
    case NET_VIEW_CREATE:
        if (!net_session_create_room(app->net, app->net_view.color)) {
            app_notice(app, "Could not create the room");
        }
        break;
    case NET_VIEW_JOIN:
        if (!net_session_join_room(app->net, app->net_view.code,
                                   app->net_view.color)) {
            app_notice(app, "Enter a 6-character room code");
        }
        break;
    case NET_VIEW_READY:
        (void)net_session_ready(app->net);
        break;
    case NET_VIEW_LEAVE:
        net_session_leave(app->net);
        break;
    case NET_VIEW_COPY_CODE:
        if (app->net->code[0] != '\0') {
            SetClipboardText(app->net->code);
            app_notice(app, "Room code copied");
        }
        break;
    case NET_VIEW_BACK:
        if (app->net->active) {
            net_session_leave(app->net);
        }
        app_request_screen(app, APP_SCREEN_MENU);
        break;
    default:
        break;
    }
}

static void app_build_net_match(App *app)
{
    NetSession *net = app->net;
    MatchConfig config = {
        .mode = GAME_MODE_PVP,
        .human_color = net->color,
        .ai = NULL
    };
    if (!match_sync_network(&app->match, &config, &net->game, net->last,
                            net->has_last, net->pass)) {
        return;
    }
    app->stats_recorded = true;
    app->last_move_age = 0.0f;
    app->net_state_seq = net->seq;
    app->net_move_pending = false;
    net->state_dirty = false;
    ui_animation_reset(&app->animation);
}

static void app_update_net_game(App *app, const UiInput *input, float dt)
{
    NetSession *net = app->net;
    if (net == NULL) {
        app->net_mode = false;
        return;
    }
    if (net->phase == NET_SESSION_LOBBY || net->phase == NET_SESSION_FAILED
        || net->phase == NET_SESSION_IDLE) {
        app->net_mode = false;
        app_request_screen(app, APP_SCREEN_NET);
        return;
    }
    if (net->error[0] != '\0') {
        app->net_move_pending = false;
    }
    if (net->state_dirty || net->seq != app->net_state_seq) {
        const int previous = app->net_state_seq;
        app_build_net_match(app);
        if (previous >= 0 && net->seq > previous) {
            audio_play(AUDIO_CUE_PLACE);
        }
    }

    const float width = (float)GetScreenWidth();
    ui_button_set_bounds(&app->net_resign,
                         (Rectangle){width - 210.0f, 12.0f, 90.0f, 30.0f});
    ui_button_set_bounds(&app->net_leave,
                         (Rectangle){width - 110.0f, 12.0f, 90.0f, 30.0f});
    app->net_resign.enabled = net->phase == NET_SESSION_PLAYING;
    app->net_leave.enabled = true;
    if (ui_button_update(&app->net_resign, input, true, dt)) {
        (void)net_session_resign(net);
    }
    if (ui_button_update(&app->net_leave, input, true, dt)) {
        net_session_leave(net);
        app->net_mode = false;
        app_request_screen(app, APP_SCREEN_NET);
        return;
    }
    if (input->escape_pressed) {
        net_session_leave(net);
        app->net_mode = false;
        app_request_screen(app, APP_SCREEN_NET);
        return;
    }

    GameViewAction action = game_view_update(
        &app->game_view, &app->match, input, true, dt);
    /* The server owns the online game: no local pause or restart. */
    if (action.type == GAME_VIEW_PAUSE || action.type == GAME_VIEW_RESTART) {
        action.type = GAME_VIEW_NONE;
    }
    app->game_view.pause.enabled = false;
    app->game_view.restart.enabled = false;
    if (action.type == GAME_VIEW_MOVE && !app->net_move_pending
        && net_session_can_move(net)) {
        if (net_session_move(net, action.move)) {
            app->net_move_pending = true;
        }
    } else if (action.type == GAME_VIEW_MENU) {
        net_session_leave(net);
        app->net_mode = false;
        app_request_screen(app, APP_SCREEN_NET);
    }
}

static void app_draw_net_hud(const App *app)
{
    const NetSession *net = app->net;
    if (net == NULL) {
        return;
    }
    const float width = (float)GetScreenWidth();
    const Rectangle panel = {12.0f, 52.0f, width - 24.0f, 48.0f};
    ui_draw_panel(panel, REVERSI_UI_RADIUS_SM,
                  ui_color_alpha(REVERSI_UI_COLOR_SURFACE, 0.92f),
                  REVERSI_UI_COLOR_BORDER);
    char text[256];
    snprintf(text, sizeof(text), "Online  room %s  you %s%s  opponent %s  %d ms",
             net->code, ui_player_name(net->color),
             net->is_host ? " (host)" : "",
             net->opponent_status[0] != '\0' ? net->opponent_status : "connected",
             net->latency_ms);
    ui_draw_text_clipped(&app->fonts, UI_TEXT_SEMIBOLD, text,
                         (Rectangle){panel.x + 12.0f, panel.y,
                                     panel.width - 240.0f, panel.height},
                         REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_LEFT);
    if (net->phase == NET_SESSION_DISCONNECTED) {
        ui_draw_text_clipped(&app->fonts, UI_TEXT_REGULAR,
                             "Reconnecting...", 
                             (Rectangle){panel.x + 12.0f, panel.y + 24.0f,
                                         panel.width - 240.0f, 18.0f},
                             REVERSI_UI_FONT_TINY,
                             REVERSI_UI_COLOR_DANGER, UI_ALIGN_LEFT);
    } else if (net->phase == NET_SESSION_FINISHED) {
        char result[128];
        snprintf(result, sizeof(result), "Game over: %s%s",
                 ui_result_title(net->result),
                 net->reason[0] != '\0' ? net->reason : "");
        ui_draw_text_clipped(&app->fonts, UI_TEXT_REGULAR, result,
                             (Rectangle){panel.x + 12.0f, panel.y + 24.0f,
                                         panel.width - 240.0f, 18.0f},
                             REVERSI_UI_FONT_TINY,
                             REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
    } else if (net->error[0] != '\0') {
        ui_draw_text_clipped(&app->fonts, UI_TEXT_REGULAR, net->error,
                             (Rectangle){panel.x + 12.0f, panel.y + 24.0f,
                                         panel.width - 240.0f, 18.0f},
                             REVERSI_UI_FONT_TINY,
                             REVERSI_UI_COLOR_DANGER, UI_ALIGN_LEFT);
    }
    ui_button_draw(&app->net_resign, &app->fonts, false);
    ui_button_draw(&app->net_leave, &app->fonts, false);
}

static void app_update_game(App *app, const UiInput *input, float dt)
{
    Match *match = &app->match;

    if (app->net_mode) {
        app_update_net_game(app, input, dt);
        return;
    }

    if (input->save_pressed) {
        app_open_saves(app, true);
        return;
    }

#ifndef NDEBUG
    if (app->autoplay && match->config.mode == GAME_MODE_PVP) {
        if (match->phase == MATCH_PHASE_PASSING_TURN) {
            (void)match_finish_pass(match);
        }
        if (match->phase == MATCH_PHASE_PLAYER_TURN) {
            app->autoplay_timer += dt;
            if (app->autoplay_timer >= 0.05f) {
                app->autoplay_timer = 0.0f;
                Move moves[REVERSI_BOARD_SIZE * REVERSI_BOARD_SIZE];
                const int count = rules_generate_legal_moves(
                    &match->state.board, match->state.current_player,
                    moves, (int)(sizeof(moves) / sizeof(moves[0])));
                if (count > 0) {
                    app_apply_move(app, moves[rand() % count]);
                }
            }
        }
    }
#endif

    app_maybe_start_ai(app);

    if (match->phase == MATCH_PHASE_AI_THINKING) {
        if (app_poll_ai(app)) {
            return;
        }

        const GameViewAction action = game_view_update(
            &app->game_view, match, input, true, dt);
        switch (action.type) {
        case GAME_VIEW_UNDO:
            app_cancel_ai(app);
            app_undo(app);
            break;
        case GAME_VIEW_RESTART:
            modal_open(&app->modal, MODAL_PURPOSE_RESTART);
            break;
        case GAME_VIEW_MENU:
            modal_open(&app->modal, MODAL_PURPOSE_RETURN_MENU);
            break;
        case GAME_VIEW_NONE:
        case GAME_VIEW_MOVE:
        case GAME_VIEW_REDO:
        case GAME_VIEW_PAUSE:
        default:
            break;
        }
        return;
    }

    if (match->phase == MATCH_PHASE_ANIMATING) {
        ui_animation_update(&app->animation, app_motion_dt(app, dt));
        if (!ui_animation_is_active(&app->animation)) {
            app_finish_animation(app);
        }

        const bool leave_animation = input->menu_pressed
            || input->restart_pressed;
        if (match->phase == MATCH_PHASE_ANIMATING
            && (input->escape_pressed || input->pause_pressed
                || leave_animation)) {
            ui_animation_finish(&app->animation);
            app_finish_animation(app);
            if (match->phase == MATCH_PHASE_PASSING_TURN) {
                (void)match_finish_pass(match);
            }
            if (match->phase == MATCH_PHASE_PLAYER_TURN
                && !leave_animation
                && (input->escape_pressed || input->pause_pressed)
                && match_pause(match)) {
                pause_view_reset(&app->pause_view);
            }
        }

        if (match->phase == MATCH_PHASE_ANIMATING) {
            (void)game_view_update(&app->game_view, match, input, false, dt);
            return;
        }
    }

    if (match->phase == MATCH_PHASE_PASSING_TURN) {
        app->pass_notice_timer -= dt;
        if (input->escape_pressed || input->pause_pressed) {
            (void)match_finish_pass(match);
            if (match->phase == MATCH_PHASE_PLAYER_TURN
                && match_pause(match)) {
                pause_view_reset(&app->pause_view);
            }
        } else if (app->pass_notice_timer <= 0.0f) {
            (void)match_finish_pass(match);
        }
    }

    if (match->phase == MATCH_PHASE_GAME_OVER && !app->stats_recorded) {
        app_complete_game_over(app);
    }

    if (match->phase == MATCH_PHASE_PAUSED) {
        (void)game_view_update(&app->game_view, match, input, false, dt);
        const PauseAction action = pause_view_update(
            &app->pause_view, input, true, dt);
        app_handle_pause_action(app, action);
        return;
    }

    if (match->phase == MATCH_PHASE_GAME_OVER) {
        app->outcome_delay -= dt;
        if (app->outcome_delay <= 0.0f) {
            result_view_reset(&app->result_view);
            app_request_screen(app, APP_SCREEN_RESULT);
            return;
        }
    }

    if (match->phase == MATCH_PHASE_PLAYER_TURN && input->history_pressed) {
        if (match_pause(match)) {
            history_view_open(&app->history_view, match);
            app_request_screen(app, APP_SCREEN_HISTORY);
        }
        return;
    }

    const bool accept = match->phase == MATCH_PHASE_PLAYER_TURN
        || match->phase == MATCH_PHASE_GAME_OVER;
    const GameViewAction action = game_view_update(
        &app->game_view, match, input, accept, dt);

    switch (action.type) {
    case GAME_VIEW_MOVE:
        app_apply_move(app, action.move);
        break;
    case GAME_VIEW_UNDO:
        app_undo(app);
        break;
    case GAME_VIEW_REDO:
        app_redo(app);
        break;
    case GAME_VIEW_PAUSE:
        if (match_pause(match)) {
            pause_view_reset(&app->pause_view);
        }
        break;
    case GAME_VIEW_RESTART:
        modal_open(&app->modal, MODAL_PURPOSE_RESTART);
        break;
    case GAME_VIEW_MENU:
        modal_open(&app->modal, MODAL_PURPOSE_RETURN_MENU);
        break;
    case GAME_VIEW_NONE:
    default:
        break;
    }
}

static void app_apply_settings(App *app)
{
    ui_theme_set((UiThemeKind)app->settings.theme);
    audio_set_volume(app->settings.volume);
    audio_set_enabled(app->settings.sfx);
}

static void app_persist_settings(App *app)
{
    app_apply_settings(app);
    if (!settings_save(&app->settings)) {
        app_notice(app, "Settings could not be saved.");
    }
}

static void app_handle_settings_action(App *app, SettingsAction action)
{
    if (action == SETTINGS_ACTION_NONE) {
        return;
    }
    if (action == SETTINGS_ACTION_BACK) {
        app_persist_settings(app);
        audio_play(AUDIO_CUE_BUTTON);
        app_request_screen(app, APP_SCREEN_MENU);
        return;
    }

    switch (action) {
    case SETTINGS_ACTION_TOGGLE_ANIMATIONS:
        app->settings.animations = !app->settings.animations;
        break;
    case SETTINGS_ACTION_SPEED_DOWN:
    case SETTINGS_ACTION_SPEED_UP:
        app->settings.animation_speed = settings_speed_next(
            app->settings.animation_speed,
            action == SETTINGS_ACTION_SPEED_UP ? 1 : -1);
        break;
    case SETTINGS_ACTION_TOGGLE_HINTS:
        app->settings.legal_hints = !app->settings.legal_hints;
        break;
    case SETTINGS_ACTION_TOGGLE_LAST_MOVE:
        app->settings.last_move_highlight =
            !app->settings.last_move_highlight;
        break;
    case SETTINGS_ACTION_TOGGLE_SFX:
        app->settings.sfx = !app->settings.sfx;
        break;
    case SETTINGS_ACTION_VOLUME_DOWN:
    case SETTINGS_ACTION_VOLUME_UP:
        app->settings.volume +=
            action == SETTINGS_ACTION_VOLUME_UP ? 5 : -5;
        if (app->settings.volume < 0) {
            app->settings.volume = 0;
        } else if (app->settings.volume > 100) {
            app->settings.volume = 100;
        }
        break;
    case SETTINGS_ACTION_DIFFICULTY_DOWN:
    case SETTINGS_ACTION_DIFFICULTY_UP:
        app->settings.default_difficulty +=
            action == SETTINGS_ACTION_DIFFICULTY_UP ? 1 : -1;
        if (app->settings.default_difficulty < 0) {
            app->settings.default_difficulty = 2;
        } else if (app->settings.default_difficulty > 2) {
            app->settings.default_difficulty = 0;
        }
        break;
    case SETTINGS_ACTION_THEME_DOWN:
    case SETTINGS_ACTION_THEME_UP:
        app->settings.theme = app->settings.theme == 0 ? 1 : 0;
        break;
    case SETTINGS_ACTION_BACK:
    case SETTINGS_ACTION_NONE:
    default:
        return;
    }

    app_persist_settings(app);
    audio_play(AUDIO_CUE_BUTTON);
}

static void app_handle_saves_action(App *app, SavesAction action)
{
    if (action.type == SAVES_ACTION_NONE) {
        return;
    }
    audio_play(AUDIO_CUE_BUTTON);

    if (action.type == SAVES_ACTION_BACK) {
        app_request_screen(app, app->saves_return);
        return;
    }

    char path[1024];
    if (!app_save_path(action.row, path, sizeof(path))) {
        app_notice(app, "Could not access the save file");
        return;
    }

    switch (action.type) {
    case SAVES_ACTION_SAVE: {
        if (action.row <= 0 || !storage_make_user_dir()) {
            return;
        }
        const SaveStatus status = save_write(
            path, &app->match, (int)app->ai_difficulty);
        app_notice(app, status == SAVE_STATUS_OK
            ? "Game saved" : save_status_message(status));
        app_refresh_slots(app);
        break;
    }
    case SAVES_ACTION_LOAD:
        (void)app_restore_saved(app, path);
        break;
    case SAVES_ACTION_DELETE:
        app->pending_delete_row = action.row;
        modal_open(&app->modal, MODAL_PURPOSE_DELETE_SAVE);
        break;
    case SAVES_ACTION_BACK:
    case SAVES_ACTION_NONE:
    default:
        break;
    }
}

static void app_update(App *app, const UiInput *input, float dt)
{
    app_update_notice(app, dt);
    if (app->transition.active) {
        app_update_transition(app, app_motion_dt(app, dt));
        return;
    }

    if (modal_is_open(&app->modal)) {
        const ModalAction action = modal_update(&app->modal, input, dt);
        if (action == MODAL_ACTION_CONFIRM) {
            audio_play(AUDIO_CUE_BUTTON);
            app_handle_modal_confirm(app);
        } else if (action == MODAL_ACTION_CANCEL) {
            audio_play(AUDIO_CUE_BUTTON);
            app->pending_delete_row = -1;
            modal_close(&app->modal);
        }
        return;
    }

    switch (app->screen) {
    case APP_SCREEN_MENU: {
        const MenuAction action = menu_view_update(
            &app->menu, input, true, dt, app->has_autosave);
        app_handle_menu_action(app, action);
        break;
    }
    case APP_SCREEN_SETUP: {
        const SetupAction action = setup_view_update(
            &app->setup_view, input, true, dt);
        if (action == SETUP_ACTION_START) {
            audio_play(AUDIO_CUE_BUTTON);
            if (app->setup_view.mode == GAME_MODE_PVE) {
                app->ai_difficulty = app_ai_difficulty_from_setup(
                    app->setup_view.difficulty);
                app->ai = ai_controller_for_difficulty(app->ai_difficulty);
            }
            MatchConfig config = {
                .mode = app->setup_view.mode,
                .human_color = app->setup_view.human_color,
                .ai = app->setup_view.mode == GAME_MODE_PVE ? app->ai : NULL
            };
            if (config.mode == GAME_MODE_PVP || config.ai != NULL) {
                app_begin_match(app, &config);
            }
        } else if (action == SETUP_ACTION_BACK) {
            audio_play(AUDIO_CUE_BUTTON);
            app_request_screen(app, APP_SCREEN_MENU);
        }
        break;
    }
    case APP_SCREEN_GAME:
        app_update_game(app, input, dt);
        break;
    case APP_SCREEN_HISTORY: {
        const HistoryAction action = history_view_update(
            &app->history_view, &app->match, input, true, dt);
        if (action == HISTORY_ACTION_BACK) {
            audio_play(AUDIO_CUE_BUTTON);
            app_request_screen(app, APP_SCREEN_GAME);
        }
        break;
    }
    case APP_SCREEN_SETTINGS: {
        const SettingsAction action = settings_view_update(
            &app->settings_view, input, true, dt, &app->settings);
        app_handle_settings_action(app, action);
        break;
    }
    case APP_SCREEN_RESULT: {
        if (input->save_pressed) {
            audio_play(AUDIO_CUE_BUTTON);
            app_open_saves(app, true);
            break;
        }
        const ResultAction action = result_view_update(
            &app->result_view, input, true, app->settings.animations,
            match_can_undo(&app->match), app_motion_dt(app, dt));
        if (action == RESULT_ACTION_PLAY_AGAIN) {
            audio_play(AUDIO_CUE_BUTTON);
            app_begin_match(app, &app->match.config);
        } else if (action == RESULT_ACTION_UNDO) {
            audio_play(AUDIO_CUE_BUTTON);
            app->pending_action = PENDING_GAME_UNDO;
            app_request_screen(app, APP_SCREEN_GAME);
        } else if (action == RESULT_ACTION_SAVE) {
            audio_play(AUDIO_CUE_BUTTON);
            app_open_saves(app, true);
        } else if (action == RESULT_ACTION_MENU) {
            audio_play(AUDIO_CUE_BUTTON);
            app_request_screen(app, APP_SCREEN_MENU);
        }
        break;
    }
    case APP_SCREEN_HELP: {
        const HelpAction action = help_view_update(
            &app->help_view, input, true, dt);
        if (action != HELP_ACTION_NONE) {
            audio_play(AUDIO_CUE_BUTTON);
        }
        if (action == HELP_ACTION_BACK) {
            app_request_screen(app, APP_SCREEN_MENU);
        }
        break;
    }
    case APP_SCREEN_STATS: {
        const StatsAction action = stats_view_update(
            &app->stats_view, input, true, dt);
        if (action == STATS_ACTION_BACK) {
            audio_play(AUDIO_CUE_BUTTON);
            app_request_screen(app, APP_SCREEN_MENU);
        }
        break;
    }
    case APP_SCREEN_SAVES: {
        const SavesAction action = saves_view_update(
            &app->saves_view, input, true, dt, app->saves_saving,
            app->save_slots);
        app_handle_saves_action(app, action);
        break;
    }
    case APP_SCREEN_NET: {
        const NetViewAction action = net_view_update(
            &app->net_view, input, dt, app->net);
        app_handle_net_action(app, action);
        if (app->net != NULL
            && (app->net->phase == NET_SESSION_PLAYING
                || app->net->phase == NET_SESSION_FINISHED)
            && !app->net_mode) {
            app_build_net_match(app);
            app->net_mode = true;
            app_request_screen(app, APP_SCREEN_GAME);
        }
        break;
    }
    case APP_SCREEN_LAB: {
        const LabAction action = lab_view_update(
            &app->lab, input, dt, app->ai_worker, &app->match,
            app->match.history.count > 1);
        if (action == LAB_ACTION_BACK) {
            audio_play(AUDIO_CUE_BUTTON);
            lab_view_leave(&app->lab, app->ai_worker);
            app_request_screen(app, APP_SCREEN_MENU);
        }
        break;
    }
    }
}

static void app_draw(const App *app)
{
    switch (app->screen) {
    case APP_SCREEN_MENU:
        menu_view_draw(&app->menu, &app->fonts);
        break;
    case APP_SCREEN_SETUP:
        setup_view_draw(&app->setup_view, &app->fonts);
        break;
    case APP_SCREEN_GAME:
        game_view_draw(&app->game_view, &app->match, &app->animation,
                       &app->fonts,
                       ui_should_show_hints(&app->match,
                                            app->settings.legal_hints),
                       app->settings.last_move_highlight,
                       app->last_move_age,
                       app->settings.animations && !app->net_mode);
        if (app->net_mode) {
            app_draw_net_hud(app);
        } else if (app->match.phase == MATCH_PHASE_PAUSED) {
            pause_view_draw(&app->pause_view, &app->fonts);
        }
        break;
    case APP_SCREEN_HISTORY:
        history_view_draw(&app->history_view, &app->match, &app->fonts);
        break;
    case APP_SCREEN_SETTINGS:
        settings_view_draw(&app->settings_view, &app->fonts, &app->settings);
        break;
    case APP_SCREEN_RESULT:
        result_view_draw(&app->result_view, &app->fonts, &app->match);
        break;
    case APP_SCREEN_HELP:
        help_view_draw(&app->help_view, &app->fonts);
        break;
    case APP_SCREEN_STATS:
        stats_view_draw(&app->stats_view, &app->fonts, &app->stats);
        break;
    case APP_SCREEN_SAVES:
        saves_view_draw(&app->saves_view, &app->fonts, app->saves_saving,
                        app->save_slots);
        break;
    case APP_SCREEN_LAB:
        lab_view_draw(&app->lab, &app->fonts);
        break;
    case APP_SCREEN_NET:
        net_view_draw(&app->net_view, app->net, &app->fonts);
        break;
    }

    app_draw_notice(app);

    if (modal_is_open(&app->modal)) {
        modal_draw(&app->modal, &app->fonts);
    }
}

static void app_draw_transition(const App *app)
{
    if (!app->transition.active) {
        return;
    }

    const float t = ui_clamp01(app->transition.t);
    const float alpha = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  ui_color_alpha(REVERSI_UI_COLOR_SCRIM, alpha));
}

typedef struct AppRun {
    App app;
    int rendered_frames;
    int max_frames;
    bool active;
} AppRun;

static bool app_boot(AppRun *run, int max_frames)
{
#ifdef __EMSCRIPTEN__
    /* raylib's Web backend divides SetWindowSize() by GetWindowScaleDPI()
       when FLAG_WINDOW_HIGHDPI is set, while the GL viewport keeps the
       divided size. That leaves most of a HiDPI canvas undrawn, so the
       browser build renders at CSS resolution instead. */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT
                   | FLAG_WINDOW_HIGHDPI);
#endif
    InitWindow(REVERSI_WINDOW_WIDTH, REVERSI_WINDOW_HEIGHT,
               "Reversi " REVERSI_VERSION_STRING);

    if (!IsWindowReady()) {
        fprintf(stderr, "Reversi: failed to initialize the game window\n");
        return false;
    }

    SetWindowMinSize(REVERSI_UI_MIN_WINDOW_WIDTH, REVERSI_UI_MIN_WINDOW_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(REVERSI_TARGET_FPS);

    memset(run, 0, sizeof(*run));
    run->max_frames = max_frames;
    app_init(&run->app);
    run->active = true;
    return true;
}

static void app_run_update(AppRun *run)
{
    App *app = &run->app;
    float dt = GetFrameTime();
    if (dt > 0.1f) {
        dt = 0.1f;
    }

    UiInput input = read_input();
    input.reduce_motion = !app->settings.animations;
    app_update(app, &input, dt);
}

static void app_run_render(AppRun *run)
{
    BeginDrawing();
    ui_draw_background();
    app_draw(&run->app);
    app_draw_transition(&run->app);
    EndDrawing();

#ifndef NDEBUG
    const char *shot_dir = getenv("REVERSI_SHOT_DIR");
    if (shot_dir != NULL && shot_dir[0] != '\0' && IsKeyPressed(KEY_F12)) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/shot-%03d.png", shot_dir,
                 run->rendered_frames);
        TakeScreenshot(path);
        TraceLog(LOG_INFO, "Reversi: screenshot %s", path);
    }
#endif

    ++run->rendered_frames;
}

static void app_frame(AppRun *run)
{
    app_run_update(run);
    app_run_render(run);
}

static void app_shutdown(AppRun *run)
{
    if (!run->active) {
        return;
    }
    App *app = &run->app;
    if (app->screen == APP_SCREEN_GAME) {
        app_autosave(app);
    }
    app_cancel_ai(app);
    if (app->ai_worker != NULL) {
        ai_worker_destroy(app->ai_worker);
        app->ai_worker = NULL;
    }
    if (app->net != NULL) {
        net_session_destroy(app->net);
        app->net = NULL;
    }
    (void)settings_save(&app->settings);
    (void)stats_save(&app->stats);
    audio_shutdown();
    ui_fonts_unload(&app->fonts);
    CloseWindow();
    run->active = false;
}

static bool app_should_stop(const AppRun *run)
{
    return !run->app.running
        || (run->max_frames > 0
            && run->rendered_frames >= run->max_frames);
}

#ifdef __EMSCRIPTEN__
int reversi_web_frames(void);
int reversi_web_mouse_x(void);
int reversi_web_mouse_y(void);
int reversi_web_screen_w(void);
int reversi_web_screen_h(void);
#ifndef NDEBUG
void reversi_web_debug_autoplay(unsigned seed);
#endif
int reversi_web_game_over(void);
int reversi_web_result(void);
int reversi_web_screen_index(void);
int reversi_web_ai_active(void);
int reversi_web_match_mode(void);
void reversi_web_cancel_ai_test(void);
int reversi_web_heap_bytes(void);
void reversi_web_resize(int width, int height);
void reversi_web_persist(void);

static AppRun g_web_run;

static void app_web_main_loop(void *arg)
{
    AppRun *run = arg;
    if (app_should_stop(run)) {
        emscripten_cancel_main_loop();
        app_shutdown(run);
        return;
    }
    app_frame(run);
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_frames(void)
{
    return g_web_run.rendered_frames;
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_mouse_x(void)
{
    return (int)GetMouseX();
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_mouse_y(void)
{
    return (int)GetMouseY();
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_screen_w(void)
{
    return GetScreenWidth();
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_screen_h(void)
{
    return GetScreenHeight();
}

#ifndef NDEBUG
EMSCRIPTEN_KEEPALIVE
void reversi_web_debug_autoplay(unsigned seed)
{
    App *app = &g_web_run.app;
    app->autoplay = true;
    app->settings.animations = false;
    app->stats_recorded = false;
    srand(seed != 0 ? seed : (unsigned)time(NULL));
    app->screen = APP_SCREEN_GAME;
}
#endif

EMSCRIPTEN_KEEPALIVE
int reversi_web_game_over(void)
{
    return g_web_run.app.match.phase == MATCH_PHASE_GAME_OVER ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_result(void)
{
    return (int)g_web_run.app.match.state.result;
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_screen_index(void)
{
    return (int)g_web_run.app.screen;
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_ai_active(void)
{
    return ai_worker_is_active(g_web_run.app.ai_worker) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_match_mode(void)
{
    return (int)g_web_run.app.match.config.mode;
}

EMSCRIPTEN_KEEPALIVE
void reversi_web_cancel_ai_test(void)
{
    app_cancel_ai(&g_web_run.app);
}

EMSCRIPTEN_KEEPALIVE
int reversi_web_heap_bytes(void)
{
    return (int)emscripten_get_heap_size();
}

EMSCRIPTEN_KEEPALIVE
void reversi_web_resize(int width, int height)
{
    SetWindowSize(width, height);
}

EMSCRIPTEN_KEEPALIVE
void reversi_web_persist(void)
{
    if (!g_web_run.active) {
        return;
    }
    App *app = &g_web_run.app;
    if (app->screen == APP_SCREEN_GAME) {
        app_autosave(app);
    }
    (void)settings_save(&app->settings);
    (void)stats_save(&app->stats);
}

int app_run(int max_frames)
{
    if (!app_boot(&g_web_run, max_frames)) {
        return 1;
    }
    emscripten_set_main_loop_arg(app_web_main_loop, &g_web_run, 0, 1);
    return 0;
}
#else
int app_run(int max_frames)
{
    AppRun run;
    if (!app_boot(&run, max_frames)) {
        return 1;
    }
    while (!WindowShouldClose() && !app_should_stop(&run)) {
        app_frame(&run);
    }
    app_shutdown(&run);
    return 0;
}
#endif
