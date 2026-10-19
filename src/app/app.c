#include "app/app.h"

#include "core/game.h"
#include "core/rules.h"
#include "ui/animation.h"
#include "ui/draw.h"
#include "ui/fonts.h"
#include "ui/game_view.h"
#include "ui/menu_view.h"
#include "ui/modal.h"
#include "ui/result_view.h"
#include "ui/settings_view.h"
#include "ui/theme.h"

#include <raylib.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_WINDOW_WIDTH 1100
#define REVERSI_WINDOW_HEIGHT 800
#define REVERSI_TARGET_FPS 60

typedef enum AppScreen {
    APP_SCREEN_MENU = 0,
    APP_SCREEN_GAME,
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
    GameView game_view;
    SettingsView settings_view;
    ResultView result_view;
    Modal modal;
    AppTransition transition;
    GameState game;
    UiMoveAnimation animation;
    Move last_move;
    bool has_last_move;
    float last_move_age;
    float outcome_delay;
    bool running;
    bool autoplay;
    float autoplay_timer;
    bool start_game_pending;
} App;

static UiInput read_input(void);
static void app_init(App *app);
static void app_new_game(App *app);
static void app_start_game(App *app);
static void app_request_screen(App *app, AppScreen target);
static void app_update_transition(App *app, float dt);
static void app_update_passive(App *app, float dt);
static void app_update(App *app, const UiInput *input, float dt);
static void app_draw(const App *app);
static void app_draw_transition(const App *app);
static void app_apply_move(App *app, Move move);
static void app_handle_menu_action(App *app, MenuAction action);
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
    app->last_move = (Move){0, 0};
    app->has_last_move = false;
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    app->running = true;
    app->autoplay_timer = 0.0f;
    app->start_game_pending = false;

#ifndef NDEBUG
    const char *autoplay = getenv("REVERSI_AUTOPLAY");
    app->autoplay = autoplay != NULL && autoplay[0] != '\0'
        && strcmp(autoplay, "0") != 0;
    if (app->autoplay) {
        app->settings.animations = false;
    }
#else
    app->autoplay = false;
#endif

    game_init(&app->game);
    ui_animation_reset(&app->animation);

    ui_fonts_load(&app->fonts);
    menu_view_init(&app->menu);
    game_view_init(&app->game_view);
    settings_view_init(&app->settings_view);
    result_view_init(&app->result_view);
    modal_init(&app->modal);
}

static void app_new_game(App *app)
{
    game_init(&app->game);
    ui_animation_reset(&app->animation);
    app->last_move = (Move){0, 0};
    app->has_last_move = false;
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;
    game_view_reset(&app->game_view);
}

static void app_switch_screen(App *app, AppScreen target)
{
    app->screen = target;
    if (target == APP_SCREEN_GAME && app->start_game_pending) {
        app_new_game(app);
        app->start_game_pending = false;
    }
}

static void app_start_game(App *app)
{
    if (app->screen == APP_SCREEN_GAME && !app->transition.active) {
        app_new_game(app);
        return;
    }

    app->start_game_pending = true;
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

        /* Reset only once the new screen is visible: the outgoing result
           screen must keep showing the finished game while it fades out. */
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
    case APP_SCREEN_GAME:
        if (app->has_last_move) {
            app->last_move_age += dt;
        }
        (void)game_view_update(&app->game_view, &app->game, &input,
                               false, false, dt);
        break;
    case APP_SCREEN_SETTINGS:
        (void)settings_view_update(
            &app->settings_view, &input, false, dt,
            app->settings.animations, app->settings.legal_hints,
            app->settings.last_move_highlight);
        break;
    case APP_SCREEN_RESULT:
        (void)result_view_update(&app->result_view, &input, false,
                                 app->settings.animations, dt);
        break;
    }
}

static void app_apply_move(App *app, Move move)
{
    const Board before = app->game.board;
    const Stone mover = app->game.current_player;

    if (!game_apply_move(&app->game, move)) {
        return;
    }

    app->last_move = move;
    app->has_last_move = true;
    app->last_move_age = 0.0f;
    app->outcome_delay = 0.0f;

    ui_animation_start(&app->animation, &before, &app->game.board,
                       move, mover, app->settings.animations);

    if (game_is_over(&app->game)) {
        app->outcome_delay = app->settings.animations ? 0.75f : 0.20f;
    }
}

static void app_handle_menu_action(App *app, MenuAction action)
{
    switch (action) {
    case MENU_ACTION_PLAY:
        app_start_game(app);
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

static void app_handle_modal_confirm(App *app)
{
    const ModalPurpose purpose = app->modal.purpose;
    modal_close(&app->modal);

    switch (purpose) {
    case MODAL_PURPOSE_QUIT:
        app->running = false;
        break;
    case MODAL_PURPOSE_RESTART:
        app_new_game(app);
        break;
    case MODAL_PURPOSE_RETURN_MENU:
        app_request_screen(app, APP_SCREEN_MENU);
        break;
    case MODAL_PURPOSE_NONE:
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
    case APP_SCREEN_GAME: {
        ui_animation_update(&app->animation, dt);
        if (app->has_last_move) {
            app->last_move_age += dt;
        }

        if (app->autoplay && !game_is_over(&app->game)
            && !ui_animation_is_active(&app->animation)) {
            app->autoplay_timer -= dt;
            if (app->autoplay_timer <= 0.0f) {
                Move moves[REVERSI_BOARD_CELL_COUNT];
                const int count = rules_generate_legal_moves(
                    &app->game.board, app->game.current_player, moves,
                    REVERSI_BOARD_CELL_COUNT);
                if (count > 0) {
                    app_apply_move(app, moves[0]);
                }
                app->autoplay_timer = 0.02f;
            }
        }

        const bool animating = ui_animation_is_active(&app->animation);
        const bool interactive = !animating;
        const bool board_accept = interactive
            && !game_is_over(&app->game);
        const GameViewAction action = game_view_update(
            &app->game_view, &app->game, input, interactive,
            board_accept, dt);

        if (action.type == GAME_VIEW_MOVE) {
            app_apply_move(app, action.move);
        } else if (action.type == GAME_VIEW_RESTART) {
            modal_open(&app->modal, MODAL_PURPOSE_RESTART);
        } else if (action.type == GAME_VIEW_MENU) {
            modal_open(&app->modal, MODAL_PURPOSE_RETURN_MENU);
        }

        if (game_is_over(&app->game)
            && !ui_animation_is_active(&app->animation)) {
            app->outcome_delay -= dt;
            if (app->outcome_delay <= 0.0f) {
                result_view_reset(&app->result_view);
                app_request_screen(app, APP_SCREEN_RESULT);
            }
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
            &app->result_view, input, true,
            app->settings.animations, dt);
        if (action == RESULT_ACTION_PLAY_AGAIN) {
            app_start_game(app);
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
    case APP_SCREEN_GAME:
        game_view_draw(&app->game_view, &app->game, &app->animation,
                       &app->fonts, app->settings.legal_hints,
                       app->settings.last_move_highlight,
                       app->last_move, app->has_last_move,
                       app->last_move_age, app->settings.animations);
        break;
    case APP_SCREEN_SETTINGS:
        settings_view_draw(&app->settings_view, &app->fonts);
        break;
    case APP_SCREEN_RESULT:
        result_view_draw(&app->result_view, &app->fonts, &app->game);
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
