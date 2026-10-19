#include "test_harness.h"

#include "ui/draw.h"

#include <stdbool.h>

static UiInput input_at(float x, float y)
{
    UiInput input = {0};
    input.mouse = (Vector2){x, y};
    return input;
}

static void test_button_defaults(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", true);

    REVERSI_TEST_ASSERT(stats, button.bounds.x == 0.0f);
    REVERSI_TEST_ASSERT(stats, button.bounds.width == 0.0f);
    REVERSI_TEST_ASSERT(stats, button.enabled);
    REVERSI_TEST_ASSERT(stats, button.primary);
    REVERSI_TEST_ASSERT(stats, !button.hovered);
    REVERSI_TEST_ASSERT(stats, !button.pressed);
}

static void test_hover_and_click(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", false);
    ui_button_set_bounds(&button, (Rectangle){10.0f, 10.0f, 100.0f, 40.0f});

    UiInput input = input_at(60.0f, 30.0f);
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, true, 0.016f));
    REVERSI_TEST_ASSERT(stats, button.hovered);
    REVERSI_TEST_ASSERT(stats, button.hover_t > 0.0f);
    REVERSI_TEST_ASSERT(stats, !button.pressed);

    input.mouse_pressed = true;
    input.mouse_down = true;
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, true, 0.016f));
    REVERSI_TEST_ASSERT(stats, button.pressed);

    input.mouse_pressed = false;
    input.mouse_down = false;
    input.mouse_released = true;
    REVERSI_TEST_ASSERT(stats, ui_button_update(&button, &input, true, 0.016f));
    REVERSI_TEST_ASSERT(stats, !button.pressed);
}

static void test_release_outside_cancels(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", false);
    ui_button_set_bounds(&button, (Rectangle){10.0f, 10.0f, 100.0f, 40.0f});

    UiInput input = input_at(60.0f, 30.0f);
    input.mouse_pressed = true;
    input.mouse_down = true;
    (void)ui_button_update(&button, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, button.pressed);

    input = input_at(200.0f, 30.0f);
    input.mouse_released = true;
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, true, 0.016f));
    REVERSI_TEST_ASSERT(stats, !button.pressed);
    REVERSI_TEST_ASSERT(stats, !button.hovered);
}

static void test_release_without_press(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", false);
    ui_button_set_bounds(&button, (Rectangle){10.0f, 10.0f, 100.0f, 40.0f});

    UiInput input = input_at(60.0f, 30.0f);
    input.mouse_released = true;
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, true, 0.016f));
}

static void test_blocked_and_disabled(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", false);
    ui_button_set_bounds(&button, (Rectangle){10.0f, 10.0f, 100.0f, 40.0f});

    UiInput input = input_at(60.0f, 30.0f);
    input.mouse_pressed = true;
    input.mouse_down = true;
    (void)ui_button_update(&button, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, button.pressed);

    input.mouse_pressed = false;
    input.mouse_down = false;
    input.mouse_released = true;
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, false, 0.016f));
    REVERSI_TEST_ASSERT(stats, !button.pressed);
    REVERSI_TEST_ASSERT(stats, !button.hovered);

    button.enabled = false;
    input = input_at(60.0f, 30.0f);
    input.mouse_pressed = true;
    input.mouse_down = true;
    (void)ui_button_update(&button, &input, true, 0.016f);
    input.mouse_pressed = false;
    input.mouse_down = false;
    input.mouse_released = true;
    REVERSI_TEST_ASSERT(stats, !ui_button_update(&button, &input, true, 0.016f));
    REVERSI_TEST_ASSERT(stats, !button.hovered);
    REVERSI_TEST_ASSERT(stats, !button.pressed);
}

static void test_reduced_motion(TestStats *stats)
{
    UiButton button;
    ui_button_init(&button, "Play", false);
    ui_button_set_bounds(&button, (Rectangle){10.0f, 10.0f, 100.0f, 40.0f});

    UiInput input = input_at(60.0f, 30.0f);
    input.reduce_motion = true;
    (void)ui_button_update(&button, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, button.hovered);
    REVERSI_TEST_ASSERT(stats, button.hover_t == 1.0f);

    input = input_at(200.0f, 30.0f);
    input.reduce_motion = true;
    (void)ui_button_update(&button, &input, true, 0.016f);
    REVERSI_TEST_ASSERT(stats, !button.hovered);
    REVERSI_TEST_ASSERT(stats, button.hover_t == 0.0f);
}

int main(void)
{
    TestStats stats = {0};

    REVERSI_RUN_TEST(&stats, test_button_defaults);
    REVERSI_RUN_TEST(&stats, test_hover_and_click);
    REVERSI_RUN_TEST(&stats, test_release_outside_cancels);
    REVERSI_RUN_TEST(&stats, test_release_without_press);
    REVERSI_RUN_TEST(&stats, test_blocked_and_disabled);
    REVERSI_RUN_TEST(&stats, test_reduced_motion);

    return reversi_test_summary(&stats, "ui.button");
}
