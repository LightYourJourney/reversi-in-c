#include "app/app.h"

#include "core/game.h"
#include "core/match.h"
#include "core/rules.h"
#include "ui/animation.h"
#include "ui/draw.h"
#include "ui/fonts.h"
#include "ui/game_view.h"
#include "ui/history_view.h"
#include "ui/menu_view.h"
#include "ui/modal.h"
#include "ui/pause_view.h"
#include "ui/result_view.h"
#include "ui/settings_view.h"
#include "ui/setup_view.h"
#include "ui/theme.h"

#include <raylib.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REVERSI_WINDOW_WIDTH 1100
#define REVERSI_WINDOW_HEIGHT 800
#define REVERSI_TARGET_FPS 60
#define REVERSI_PASS_NOTICE_SECONDS 0.9f

typedef enum AppScreen {
    APP_SCREEN_MENU = 0,
    APP_SCREEN_SETUP,
    APP_SCREEN_GAME,
    APP_SCREEN_HISTORY,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_RESULT
} AppScreen;

typedef struct AppSettings {
    bool animations;
    bool legal_hints;
    bool last_move_highlight;
} AppSettings;

typedef struct AppTransition {
    bool active;
    bool switched;
    float t;
    float duration;
    AppScreen target;
} AppTransition;

typedef struct App {
    AppScreen screen;
    AppSettings settings;
    UiFonts fonts;
    MenuView menu;
    SetupView setup_view;
    GameView game_view;
    PauseView pause_view;
    HistoryView history_view;
    SettingsView settings_view;
    ResultView result_view;
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
    bool start_match_pending;
    bool undo_to_game_pending;
    MatchConfig pending_config;
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
static void app_handle_menu_action(App *app, MenuAction action);
static void app_handle_pause_action(App *app, PauseAction action);
static void app_handle_modal_confirm(App *app);

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
    return input;
}

static void app_init(App *app)
{
    app->screen = APP_SCREEN_MENU;
    app->settings = (AppSettings){
        .animations = true,
        .legal_hints = true,
        .last_move_highlight = true
    };
    app->transition = (AppTransition){0};
    app->modal = (Modal){0};
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->pass_notice_timer = 0.0f;
    app->running = true;
    app->autoplay_timer = 0.0f;
    app->start_match_pending = false;
    app->undo_to_game_pending = false;
    app->pending_config = (MatchConfig){
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };

#ifndef NDEBUG
    const char *autoplay = getenv("REVERSI_AUTOPLAY");
    app->autoplay = autoplay != NULL && autoplay[0] != '\0'
        && strcmp(autoplay, "0") != 0;
    if (app->autoplay) {
        app->settings.animations = false;
        const unsigned long seed = strtoul(autoplay, NULL, 10);
        srand(seed != 0 ? (unsigned)seed : (unsigned)time(NULL));
    }
#else
    app->autoplay = false;
#endif

    ui_fonts_load(&app->fonts);
    menu_view_init(&app->menu);
    setup_view_init(&app->setup_view);
    setup_view_set_ai_available(&app->setup_view, false);
    game_view_init(&app->game_view);
    pause_view_init(&app->pause_view);
    history_view_init(&app->history_view);
    settings_view_init(&app->settings_view);
    result_view_init(&app->result_view);
    modal_init(&app->modal);
    ui_animation_reset(&app->animation);

    MatchConfig default_config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    (void)match_start(&app->match, &default_config);

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

static void app_switch_screen(App *app, AppScreen target)
{
    app->screen = target;
    if (target != APP_SCREEN_GAME) {
        return;
    }

    if (app->start_match_pending) {
        if (match_start(&app->match, &app->pending_config)) {
            app_reset_match_ui(app);
        }
        app->start_match_pending = false;
        app->undo_to_game_pending = false;
    } else if (app->undo_to_game_pending) {
        if (match_undo(&app->match)) {
            app_reset_match_ui(app);
        }
        app->undo_to_game_pending = false;
    }
}

static void app_begin_match(App *app, const MatchConfig *config)
{
    if (config == NULL) {
        return;
    }

    if (app->screen == APP_SCREEN_GAME && !app->transition.active) {
        if (match_start(&app->match, config)) {
            app_reset_match_ui(app);
        }
        return;
    }

    app->pending_config = *config;
    app->start_match_pending = true;
    app->undo_to_game_pending = false;
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

    switch (app->screen) {
    case APP_SCREEN_MENU:
        (void)menu_view_update(&app->menu, &input, false, dt);
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
            &app->settings_view, &input, false, dt,
            app->settings.animations, app->settings.legal_hints,
            app->settings.last_move_highlight);
        break;
    case APP_SCREEN_RESULT:
        (void)result_view_update(&app->result_view, &input, false,
                                 app->settings.animations,
                                 match_can_undo(&app->match), dt);
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
            ? 0.0f : REVERSI_PASS_NOTICE_SECONDS;
        break;
    case MATCH_PHASE_GAME_OVER:
        app->outcome_delay = app->settings.animations ? 0.75f : 0.20f;
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
    ui_animation_start(&app->animation, &before, &app->match.state.board,
                       move, mover, app->settings.animations);

    if (!ui_animation_is_active(&app->animation)) {
        app_finish_animation(app);
    }
}

static void app_undo(App *app)
{
    if (!match_undo(&app->match)) {
        return;
    }

    ui_animation_reset(&app->animation);
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->pass_notice_timer = 0.0f;
    game_view_reset(&app->game_view);
}

static void app_redo(App *app)
{
    if (!match_redo(&app->match)) {
        return;
    }

    ui_animation_reset(&app->animation);
    app->last_move_age = 0.0f;
    app->pass_notice_timer = 0.0f;
    game_view_reset(&app->game_view);

    if (app->match.phase == MATCH_PHASE_GAME_OVER) {
        app->outcome_delay = app->settings.animations ? 0.75f : 0.20f;
    } else {
        app->outcome_delay = 0.0f;
    }
}

static void app_handle_menu_action(App *app, MenuAction action)
{
    switch (action) {
    case MENU_ACTION_PLAY:
        app_request_screen(app, APP_SCREEN_SETUP);
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
    switch (action) {
    case PAUSE_ACTION_RESUME:
        (void)match_resume(&app->match);
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
        app->running = false;
        break;
    case MODAL_PURPOSE_RESTART:
        if (match_restart(&app->match)) {
            app_reset_match_ui(app);
        }
        break;
    case MODAL_PURPOSE_RETURN_MENU:
        app_request_screen(app, APP_SCREEN_MENU);
        break;
    case MODAL_PURPOSE_NONE:
    default:
        break;
    }
}

static void app_update_game(App *app, const UiInput *input, float dt)
{
    Match *match = &app->match;

#ifndef NDEBUG
    if (app->autoplay) {
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

    if (match->phase == MATCH_PHASE_ANIMATING) {
        ui_animation_update(&app->animation, dt);
        if (!ui_animation_is_active(&app->animation)) {
            app_finish_animation(app);
        }

        if (match->phase == MATCH_PHASE_ANIMATING
            && (input->escape_pressed || input->pause_pressed
                || input->menu_pressed)) {
            ui_animation_finish(&app->animation);
            app_finish_animation(app);
            if (match->phase == MATCH_PHASE_PASSING_TURN) {
                (void)match_finish_pass(match);
            }
            if (match->phase == MATCH_PHASE_PLAYER_TURN
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

static void app_update(App *app, const UiInput *input, float dt)
{
    if (app->transition.active) {
        app_update_transition(app, dt);
        return;
    }

    if (modal_is_open(&app->modal)) {
        const ModalAction action = modal_update(&app->modal, input, dt);
        if (action == MODAL_ACTION_CONFIRM) {
            app_handle_modal_confirm(app);
        } else if (action == MODAL_ACTION_CANCEL) {
            modal_close(&app->modal);
        }
        return;
    }

    switch (app->screen) {
    case APP_SCREEN_MENU: {
        const MenuAction action = menu_view_update(
            &app->menu, input, true, dt);
        app_handle_menu_action(app, action);
        break;
    }
    case APP_SCREEN_SETUP: {
        const SetupAction action = setup_view_update(
            &app->setup_view, input, true, dt);
        if (action == SETUP_ACTION_START) {
            MatchConfig config = {
                .mode = app->setup_view.mode,
                .human_color = app->setup_view.human_color,
                .ai = NULL
            };
            if (config.mode == GAME_MODE_PVP) {
                app_begin_match(app, &config);
            }
        } else if (action == SETUP_ACTION_BACK) {
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
            app_request_screen(app, APP_SCREEN_GAME);
        }
        break;
    }
    case APP_SCREEN_SETTINGS: {
        const SettingsAction action = settings_view_update(
            &app->settings_view, input, true, dt,
            app->settings.animations, app->settings.legal_hints,
            app->settings.last_move_highlight);

        switch (action) {
        case SETTINGS_ACTION_TOGGLE_ANIMATIONS:
            app->settings.animations = !app->settings.animations;
            break;
        case SETTINGS_ACTION_TOGGLE_HINTS:
            app->settings.legal_hints = !app->settings.legal_hints;
            break;
        case SETTINGS_ACTION_TOGGLE_LAST_MOVE:
            app->settings.last_move_highlight =
                !app->settings.last_move_highlight;
            break;
        case SETTINGS_ACTION_BACK:
            app_request_screen(app, APP_SCREEN_MENU);
            break;
        case SETTINGS_ACTION_NONE:
        default:
            break;
        }
        break;
    }
    case APP_SCREEN_RESULT: {
        const ResultAction action = result_view_update(
            &app->result_view, input, true, app->settings.animations,
            match_can_undo(&app->match), dt);
        if (action == RESULT_ACTION_PLAY_AGAIN) {
            app_begin_match(app, &app->match.config);
        } else if (action == RESULT_ACTION_UNDO) {
            app->undo_to_game_pending = true;
            app->start_match_pending = false;
            app_request_screen(app, APP_SCREEN_GAME);
        } else if (action == RESULT_ACTION_MENU) {
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
                       &app->fonts, app->settings.legal_hints,
                       app->settings.last_move_highlight,
                       app->last_move_age, app->settings.animations);
        if (app->match.phase == MATCH_PHASE_PAUSED) {
            pause_view_draw(&app->pause_view, &app->fonts);
        }
        break;
    case APP_SCREEN_HISTORY:
        history_view_draw(&app->history_view, &app->match, &app->fonts);
        break;
    case APP_SCREEN_SETTINGS:
        settings_view_draw(&app->settings_view, &app->fonts);
        break;
    case APP_SCREEN_RESULT:
        result_view_draw(&app->result_view, &app->fonts, &app->match);
        break;
    }

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

int app_run(int max_frames)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT
                   | FLAG_WINDOW_HIGHDPI);
    InitWindow(REVERSI_WINDOW_WIDTH, REVERSI_WINDOW_HEIGHT, "Reversi");

    if (!IsWindowReady()) {
        fprintf(stderr, "Reversi: failed to initialize the game window\n");
        return 1;
    }

    SetWindowMinSize(REVERSI_UI_MIN_WINDOW_WIDTH, REVERSI_UI_MIN_WINDOW_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(REVERSI_TARGET_FPS);

    App app;
    app_init(&app);

    int rendered_frames = 0;
    while (!WindowShouldClose() && app.running) {
        float dt = GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        UiInput input = read_input();
        input.reduce_motion = !app.settings.animations;
        app_update(&app, &input, dt);

        BeginDrawing();
        ui_draw_background();
        app_draw(&app);
        app_draw_transition(&app);
        EndDrawing();

#ifndef NDEBUG
        const char *shot_dir = getenv("REVERSI_SHOT_DIR");
        if (shot_dir != NULL && shot_dir[0] != '\0'
            && IsKeyPressed(KEY_F12)) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/shot-%03d.png",
                     shot_dir, rendered_frames);
            TakeScreenshot(path);
            TraceLog(LOG_INFO, "Reversi: screenshot %s", path);
        }
#endif

        ++rendered_frames;
        if (max_frames > 0 && rendered_frames >= max_frames) {
            break;
        }
    }

    ui_fonts_unload(&app.fonts);
    CloseWindow();
    return 0;
}
