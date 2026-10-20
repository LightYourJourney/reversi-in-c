#include "test_harness.h"
#include "test_helpers.h"

#include "core/game.h"
#include "core/match.h"
#include "ui/animation.h"
#include "ui/board_view.h"
#include "ui/draw.h"
#include "ui/game_view.h"
#include "ui/history_view.h"
#include "ui/pause_view.h"
#include "ui/result_view.h"
#include "ui/setup_view.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static bool nearly_equal(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static void test_ui_labels_and_focus(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_player_name(STONE_BLACK), "Black") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_player_name(STONE_WHITE), "White") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_player_name(STONE_EMPTY), "None") == 0);

    REVERSI_TEST_ASSERT(stats, ui_result_title(GAME_RESULT_ONGOING) == NULL);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_result_title(GAME_RESULT_BLACK_WIN), "Black wins") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_result_title(GAME_RESULT_WHITE_WIN), "White wins") == 0);
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_result_title(GAME_RESULT_DRAW), "Draw") == 0);

    Match title_match;
    memset(&title_match, 0, sizeof(title_match));
    title_match.config.mode = GAME_MODE_PVE;
    title_match.config.human_color = STONE_BLACK;
    title_match.state.result = GAME_RESULT_BLACK_WIN;
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_match_result_title(&title_match), "You win") == 0);
    title_match.state.result = GAME_RESULT_WHITE_WIN;
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_match_result_title(&title_match), "Computer wins") == 0);
    title_match.state.result = GAME_RESULT_DRAW;
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_match_result_title(&title_match), "Draw") == 0);
    title_match.config.mode = GAME_MODE_PVP;
    title_match.state.result = GAME_RESULT_BLACK_WIN;
    REVERSI_TEST_ASSERT(stats,
        strcmp(ui_match_result_title(&title_match), "Black wins") == 0);
    title_match.state.result = GAME_RESULT_ONGOING;
    REVERSI_TEST_ASSERT(stats, ui_match_result_title(&title_match) == NULL);
    REVERSI_TEST_ASSERT(stats, ui_match_result_title(NULL) == NULL);

    REVERSI_TEST_ASSERT(stats, ui_focus_step(0, 1, 4, 1) == 1);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(3, 1, 4, 1) == 1);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(2, -1, 4, 1) == 1);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(0, 1, 3, 2) == 2);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(0, 1, 3, 6) == 0);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(2, 1, 3, 4) == 0);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(1, 1, 3, 1) == 2);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(1, -1, 3, 1) == 2);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(0, 1, 0, 1) == 0);
    REVERSI_TEST_ASSERT(stats, ui_focus_step(0, 1, 1, 0) == 0);
}

static void test_clamp_and_easing(TestStats *stats)
{
    REVERSI_TEST_ASSERT(stats, nearly_equal(ui_clamp01(-1.0f), 0.0f));
    REVERSI_TEST_ASSERT(stats, nearly_equal(ui_clamp01(0.25f), 0.25f));
    REVERSI_TEST_ASSERT(stats, nearly_equal(ui_clamp01(2.0f), 1.0f));

    REVERSI_TEST_ASSERT(stats, nearly_equal(ui_ease_out_back(0.0f), 0.0f));
    REVERSI_TEST_ASSERT(stats, nearly_equal(ui_ease_out_back(1.0f), 1.0f));
    REVERSI_TEST_ASSERT(stats, ui_ease_out_back(0.5f) > 1.0f);
    REVERSI_TEST_ASSERT(stats, ui_ease_out_back(2.0f) <= 1.0001f);
}

static void test_color_alpha(TestStats *stats)
{
    const Color base = {10, 20, 30, 200};

    const Color half = ui_color_alpha(base, 0.5f);
    REVERSI_TEST_ASSERT(stats, half.r == 10 && half.g == 20 && half.b == 30);
    REVERSI_TEST_ASSERT(stats, half.a == 100);

    REVERSI_TEST_ASSERT(stats, ui_color_alpha(base, -1.0f).a == 0);
    REVERSI_TEST_ASSERT(stats, ui_color_alpha(base, 2.0f).a == 200);
}

static void build_d3_move(TestStats *stats, Board *before, Board *after,
                          Move *move)
{
    GameState game;
    game_init(&game);
    *before = game.board;
    *move = (Move){.row = 5, .col = 3}; /* d3 */
    REVERSI_TEST_ASSERT(stats, game_apply_move(&game, *move));
    *after = game.board;
}

static void test_animation_requires_enabled(TestStats *stats)
{
    Board before;
    Board after;
    Move move;
    build_d3_move(stats, &before, &after, &move);

    UiMoveAnimation animation;
    ui_animation_start(&animation, &before, &after, move, STONE_BLACK, false);
    REVERSI_TEST_ASSERT(stats, !ui_animation_is_active(&animation));
    ui_animation_update(&animation, 1.0f);
    REVERSI_TEST_ASSERT(stats, !ui_animation_is_active(&animation));
}

static void test_animation_placement_and_flip(TestStats *stats)
{
    Board before;
    Board after;
    Move move;
    build_d3_move(stats, &before, &after, &move);
    const Board before_copy = before;
    const Board after_copy = after;

    UiMoveAnimation animation;
    ui_animation_start(&animation, &before, &after, move, STONE_BLACK, true);

    REVERSI_TEST_ASSERT(stats, ui_animation_is_active(&animation));
    REVERSI_TEST_ASSERT(stats, animation.flip_count == 1);
    REVERSI_TEST_ASSERT(stats, animation.flips[0].row == 4);
    REVERSI_TEST_ASSERT(stats, animation.flips[0].col == 3);
    REVERSI_TEST_ASSERT(stats, animation.duration > animation.place_duration);

    Stone stone = STONE_EMPTY;
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    float alpha = 0.0f;

    REVERSI_TEST_ASSERT(stats, ui_animation_override(
        &animation, 5, 3, &stone, &scale_x, &scale_y, &alpha));
    REVERSI_TEST_ASSERT(stats, stone == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, scale_x < 0.01f && scale_y < 0.01f);
    REVERSI_TEST_ASSERT(stats, alpha < 0.01f);

    REVERSI_TEST_ASSERT(stats, ui_animation_override(
        &animation, 4, 3, &stone, &scale_x, &scale_y, &alpha));
    REVERSI_TEST_ASSERT(stats, stone == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, nearly_equal(scale_x, 1.0f));

    ui_animation_update(&animation, animation.flips[0].delay
                        + animation.flip_duration * 0.25f);

    REVERSI_TEST_ASSERT(stats, ui_animation_override(
        &animation, 4, 3, &stone, &scale_x, &scale_y, &alpha));
    REVERSI_TEST_ASSERT(stats, stone == STONE_WHITE);
    REVERSI_TEST_ASSERT(stats, scale_x < 0.6f && scale_x > 0.4f);

    animation.elapsed = animation.flips[0].delay
        + animation.flip_duration * 0.75f;
    REVERSI_TEST_ASSERT(stats, ui_animation_override(
        &animation, 4, 3, &stone, &scale_x, &scale_y, &alpha));
    REVERSI_TEST_ASSERT(stats, stone == STONE_BLACK);
    REVERSI_TEST_ASSERT(stats, scale_x < 0.6f && scale_x > 0.4f);

    animation.elapsed = animation.duration;
    REVERSI_TEST_ASSERT(stats, !ui_animation_override(
        &animation, 4, 3, &stone, &scale_x, &scale_y, &alpha));

    ui_animation_update(&animation, 1.0f);
    REVERSI_TEST_ASSERT(stats, !ui_animation_is_active(&animation));

    REVERSI_TEST_ASSERT(stats, boards_equal(&before, &before_copy));
    REVERSI_TEST_ASSERT(stats, boards_equal(&after, &after_copy));
}

static void test_board_view_hit_test(TestStats *stats)
{
    const Rectangle board = {10.0f, 20.0f, 100.0f, 100.0f};
    int row = -1;
    int col = -1;

    REVERSI_TEST_ASSERT(stats, board_view_hit_test(
        board, 12.5f, (Vector2){10.0f, 20.0f}, &row, &col));
    REVERSI_TEST_ASSERT(stats, row == 0 && col == 0);

    REVERSI_TEST_ASSERT(stats, board_view_hit_test(
        board, 12.5f, (Vector2){109.0f, 119.0f}, &row, &col));
    REVERSI_TEST_ASSERT(stats, row == 7 && col == 7);

    REVERSI_TEST_ASSERT(stats, !board_view_hit_test(
        board, 12.5f, (Vector2){9.0f, 20.0f}, &row, &col));
    REVERSI_TEST_ASSERT(stats, !board_view_hit_test(
        board, 12.5f, (Vector2){110.0f, 20.0f}, &row, &col));
    REVERSI_TEST_ASSERT(stats, !board_view_hit_test(
        board, 0.0f, (Vector2){20.0f, 30.0f}, NULL, NULL));
    REVERSI_TEST_ASSERT(stats, board_view_hit_test(
        board, 12.5f, (Vector2){20.0f, 30.0f}, NULL, NULL));
}

static void test_board_view_legality(TestStats *stats)
{
    GameState game;
    game_init(&game);

    REVERSI_TEST_ASSERT(stats, board_view_cell_is_legal(&game, 5, 3));
    REVERSI_TEST_ASSERT(stats, !board_view_cell_is_legal(&game, 3, 3));
    REVERSI_TEST_ASSERT(stats, !board_view_cell_is_legal(&game, -1, 0));
    REVERSI_TEST_ASSERT(stats, !board_view_cell_is_legal(NULL, 5, 3));

    (void)game_apply_move(&game, (Move){.row = 5, .col = 3});
    REVERSI_TEST_ASSERT(stats, game.result == GAME_RESULT_ONGOING);
    REVERSI_TEST_ASSERT(stats, !board_view_cell_is_legal(&game, 5, 3));
}

static void test_focus_update(TestStats *stats)
{
    int focus = 0;
    bool visible = false;
    UiInput input = {0};

    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 0 && !visible);

    input.down_pressed = true;
    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 1 && visible);

    input.down_pressed = false;
    input.up_pressed = true;
    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 0 && visible);

    focus = 0;
    input.up_pressed = false;
    input.down_pressed = true;
    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 1 && visible);

    focus = 0;
    input.down_pressed = false;
    input.up_pressed = true;
    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 2 && visible);

    input.up_pressed = false;
    input.mouse_pressed = true;
    ui_focus_update(&focus, &visible, 3, &input);
    REVERSI_TEST_ASSERT(stats, focus == 2 && !visible);

    ui_focus_update(NULL, &visible, 3, &input);
    ui_focus_update(&focus, NULL, 3, &input);
    ui_focus_update(&focus, &visible, 0, &input);
    ui_focus_update(&focus, &visible, 3, NULL);
}

static void test_game_view_undo_redo_hooks(TestStats *stats)
{
    GameView view;
    game_view_init(&view);
    Match match;
    const MatchConfig config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 5, .col = 3}));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_ANIMATING);
    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    input.undo_pressed = true;
    GameViewAction action = game_view_update(
        &view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_UNDO);
    REVERSI_TEST_ASSERT(stats, view.undo.enabled);

    REVERSI_TEST_ASSERT(stats, match_undo(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    input.undo_pressed = false;
    input.redo_pressed = true;
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_REDO);
    REVERSI_TEST_ASSERT(stats, view.redo.enabled);

    input.redo_pressed = false;
    input.pause_pressed = true;
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_PAUSE);
    REVERSI_TEST_ASSERT(stats, match_pause(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PAUSED);
    REVERSI_TEST_ASSERT(stats, match_resume(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);

    input.pause_pressed = false;
    input.mouse_pressed = true;
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 4, .col = 2}));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_ANIMATING);
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, !view.undo.enabled);
    REVERSI_TEST_ASSERT(stats, !view.pause.enabled);
}

static void test_setup_view_pvp_flow(TestStats *stats)
{
    SetupView view;
    setup_view_init(&view);
    setup_view_set_ai_available(&view, false);
    REVERSI_TEST_ASSERT(stats, view.mode == GAME_MODE_PVP);
    REVERSI_TEST_ASSERT(stats, view.human_color == STONE_BLACK);

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, !view.computer.enabled);
    REVERSI_TEST_ASSERT(stats, view.start.enabled);

    input.enter_pressed = true;
    SetupAction action = setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action == SETUP_ACTION_NONE);

    input.enter_pressed = false;
    input.down_pressed = true;
    action = setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action == SETUP_ACTION_NONE);
    REVERSI_TEST_ASSERT(stats, view.focus == 3);

    input.down_pressed = false;
    input.enter_pressed = true;
    action = setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action == SETUP_ACTION_START);

    input.enter_pressed = false;
    input.escape_pressed = true;
    action = setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action == SETUP_ACTION_BACK);
}

static void test_setup_view_pve_flow(TestStats *stats)
{
    SetupView view;
    setup_view_init(&view);
    setup_view_set_ai_available(&view, true);

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.computer.enabled);
    REVERSI_TEST_ASSERT(stats, !view.easy.enabled);
    REVERSI_TEST_ASSERT(stats, !view.hard.enabled);
    REVERSI_TEST_ASSERT(stats, view.start.enabled);
    REVERSI_TEST_ASSERT(stats, view.difficulty == SETUP_DIFFICULTY_MEDIUM);

    input.enter_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.mode == GAME_MODE_PVE);
    input.enter_pressed = false;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.easy.enabled);
    REVERSI_TEST_ASSERT(stats, view.hard.enabled);
    REVERSI_TEST_ASSERT(stats, view.white.enabled);

    input.enter_pressed = false;
    input.down_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.focus == 1);

    input.down_pressed = false;
    input.enter_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.human_color == STONE_WHITE);

    input.enter_pressed = false;
    input.down_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.focus == 2);

    input.down_pressed = false;
    input.right_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.difficulty == SETUP_DIFFICULTY_HARD);

    input.right_pressed = false;
    input.enter_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.difficulty == SETUP_DIFFICULTY_EASY);

    input.enter_pressed = false;
    input.left_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.difficulty == SETUP_DIFFICULTY_HARD);

    input.left_pressed = false;
    input.down_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.focus == 3);

    input.down_pressed = false;
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        setup_view_update(&view, &input, true, 0.016f)
        == SETUP_ACTION_START);

    input.enter_pressed = false;
    input.down_pressed = true;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, view.focus == 4);
    input.down_pressed = false;
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        setup_view_update(&view, &input, true, 0.016f)
        == SETUP_ACTION_BACK);

    input.enter_pressed = false;
    view.mode = GAME_MODE_PVP;
    (void)setup_view_update(&view, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, !view.white.enabled);
    REVERSI_TEST_ASSERT(stats, !view.medium.enabled);
}

static bool dummy_ai_select(void *context, const GameState *state,
                            Move *out_move)
{
    (void)context;
    (void)state;
    (void)out_move;
    return false;
}

static void test_game_view_ai_thinking(TestStats *stats)
{
    static const AiController controller = {
        .name = "Test AI",
        .select_move = dummy_ai_select,
        .context = NULL
    };
    const MatchConfig config = {
        .mode = GAME_MODE_PVE,
        .human_color = STONE_BLACK,
        .ai = &controller
    };
    Match match;
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    REVERSI_TEST_ASSERT(stats,
        match_apply_move(&match, (Move){.row = 5, .col = 3}));
    REVERSI_TEST_ASSERT(stats, match_finish_animation(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
    REVERSI_TEST_ASSERT(stats, !match_is_human_turn(&match));
    REVERSI_TEST_ASSERT(stats, match_begin_ai_turn(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_AI_THINKING);

    GameView view;
    game_view_init(&view);
    view.cursor_row = 4;
    view.cursor_col = 2;
    view.cursor_visible = true;
    view.keyboard_mode = true;

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    GameViewAction action = game_view_update(
        &view, &match, &input, true, 0.5f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, view.thinking_time > 0.49f);
    REVERSI_TEST_ASSERT(stats, !view.cursor_visible);
    REVERSI_TEST_ASSERT(stats, view.undo.enabled);
    REVERSI_TEST_ASSERT(stats, !view.redo.enabled);
    REVERSI_TEST_ASSERT(stats, !view.pause.enabled);
    REVERSI_TEST_ASSERT(stats, view.restart.enabled);
    REVERSI_TEST_ASSERT(stats, view.menu.enabled);

    input.enter_pressed = true;
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_AI_THINKING);

    input.enter_pressed = false;
    input.pause_pressed = true;
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_AI_THINKING);

    input.pause_pressed = false;
    input.undo_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        game_view_update(&view, &match, &input, true, 0.016f).type
        == GAME_VIEW_UNDO);

    input.undo_pressed = false;
    input.restart_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        game_view_update(&view, &match, &input, true, 0.016f).type
        == GAME_VIEW_RESTART);

    input.restart_pressed = false;
    input.menu_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        game_view_update(&view, &match, &input, true, 0.016f).type
        == GAME_VIEW_MENU);

    REVERSI_TEST_ASSERT(stats, match_cancel_ai_turn(&match));
    REVERSI_TEST_ASSERT(stats, match.phase == MATCH_PHASE_PLAYER_TURN);
}

static void test_pause_and_history_views(TestStats *stats)
{
    PauseView pause;
    pause_view_init(&pause);
    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        pause_view_update(&pause, &input, true, 0.016f)
        == PAUSE_ACTION_RESUME);

    HistoryView history;
    history_view_init(&history);
    Match match;
    const MatchConfig config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));
    history_view_open(&history, &match);
    REVERSI_TEST_ASSERT(stats, history.scroll == 0);

    UiInput back = {0};
    back.mouse = (Vector2){-1000.0f, -1000.0f};
    back.escape_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        history_view_update(&history, &match, &back, true, 0.016f)
        == HISTORY_ACTION_BACK);
}

static void test_result_view_actions(TestStats *stats)
{
    ResultView view;
    result_view_init(&view);
    Match match;
    const MatchConfig config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        result_view_update(&view, &input, true, false, false, 0.016f)
        == RESULT_ACTION_PLAY_AGAIN);
    REVERSI_TEST_ASSERT(stats, !view.undo.enabled);

    result_view_reset(&view);
    input.enter_pressed = false;
    input.down_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        result_view_update(&view, &input, true, false, true, 0.016f)
        == RESULT_ACTION_NONE);
    REVERSI_TEST_ASSERT(stats, view.focus == 1);
    REVERSI_TEST_ASSERT(stats, view.undo.enabled);

    input.down_pressed = false;
    input.enter_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        result_view_update(&view, &input, true, false, true, 0.016f)
        == RESULT_ACTION_UNDO);

    result_view_reset(&view);
    view.focus = 2;
    input.enter_pressed = true;
    input.escape_pressed = false;
    REVERSI_TEST_ASSERT(stats,
        result_view_update(&view, &input, true, false, true, 0.016f)
        == RESULT_ACTION_SAVE);

    result_view_reset(&view);
    input.enter_pressed = false;
    input.escape_pressed = true;
    REVERSI_TEST_ASSERT(stats,
        result_view_update(&view, &input, true, false, true, 0.016f)
        == RESULT_ACTION_MENU);
}

static void test_game_view_enter_and_gating(TestStats *stats)
{
    GameView view;
    game_view_init(&view);
    Match match;
    const MatchConfig config = {
        .mode = GAME_MODE_PVP,
        .human_color = STONE_BLACK,
        .ai = NULL
    };
    REVERSI_TEST_ASSERT(stats, match_start(&match, &config));

    UiInput input = {0};
    input.mouse = (Vector2){-1000.0f, -1000.0f};
    input.enter_pressed = true;

    GameViewAction action = game_view_update(
        &view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, !view.cursor_visible);

    view.cursor_row = 5;
    view.cursor_col = 3;
    view.cursor_visible = true;
    view.keyboard_mode = true;
    action = game_view_update(&view, &match, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_MOVE);
    REVERSI_TEST_ASSERT(stats, action.move.row == 5 && action.move.col == 3);

    UiInput restart = {0};
    restart.mouse = (Vector2){-1000.0f, -1000.0f};
    restart.restart_pressed = true;
    action = game_view_update(&view, &match, &restart, false, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_NONE);
    REVERSI_TEST_ASSERT(stats, !view.restart.enabled);

    action = game_view_update(&view, &match, &restart, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, action.type == GAME_VIEW_RESTART);
    REVERSI_TEST_ASSERT(stats, view.restart.enabled);
}

static void test_animation_finish(TestStats *stats)
{
    Board before;
    Board after;
    Move move;
    build_d3_move(stats, &before, &after, &move);

    UiMoveAnimation animation;
    ui_animation_start(&animation, &before, &after, move, STONE_BLACK, true);
    REVERSI_TEST_ASSERT(stats, ui_animation_is_active(&animation));

    ui_animation_finish(&animation);
    REVERSI_TEST_ASSERT(stats, !ui_animation_is_active(&animation));
    REVERSI_TEST_ASSERT(stats,
        nearly_equal(animation.elapsed, animation.duration));

    const float elapsed = animation.elapsed;
    ui_animation_finish(&animation);
    REVERSI_TEST_ASSERT(stats, nearly_equal(animation.elapsed, elapsed));
    ui_animation_finish(NULL);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_ui_labels_and_focus);
    REVERSI_RUN_TEST(&stats, test_clamp_and_easing);
    REVERSI_RUN_TEST(&stats, test_color_alpha);
    REVERSI_RUN_TEST(&stats, test_focus_update);
    REVERSI_RUN_TEST(&stats, test_board_view_hit_test);
    REVERSI_RUN_TEST(&stats, test_board_view_legality);
    REVERSI_RUN_TEST(&stats, test_game_view_enter_and_gating);
    REVERSI_RUN_TEST(&stats, test_game_view_undo_redo_hooks);
    REVERSI_RUN_TEST(&stats, test_setup_view_pvp_flow);
    REVERSI_RUN_TEST(&stats, test_setup_view_pve_flow);
    REVERSI_RUN_TEST(&stats, test_game_view_ai_thinking);
    REVERSI_RUN_TEST(&stats, test_pause_and_history_views);
    REVERSI_RUN_TEST(&stats, test_result_view_actions);
    REVERSI_RUN_TEST(&stats, test_animation_requires_enabled);
    REVERSI_RUN_TEST(&stats, test_animation_placement_and_flip);
    REVERSI_RUN_TEST(&stats, test_animation_finish);

    return reversi_test_summary(&stats, "ui.helpers");
}
