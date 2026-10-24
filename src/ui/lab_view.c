#include "ui/lab_view.h"

#include "analysis/record.h"
#include "core/rules.h"
#include "ui/board_view.h"
#include "ui/theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LAB_MARGIN 20.0f
#define LAB_TOP_HEIGHT 50.0f
#define LAB_BOTTOM_HEIGHT 92.0f
#define LAB_NOTICE_SECONDS 4.0f

static const float LAB_SPEEDS[] = {0.4f, 0.8f, 1.5f};
static const int LAB_SPEED_COUNT =
    (int)(sizeof(LAB_SPEEDS) / sizeof(LAB_SPEEDS[0]));

static void lab_notice(LabView *view, const char *text)
{
    snprintf(view->notice, sizeof(view->notice), "%s", text);
    view->notice_timer = LAB_NOTICE_SECONDS;
}

static int lab_black_score(int side_to_move_score, Stone player)
{
    return player == STONE_BLACK ? side_to_move_score : -side_to_move_score;
}

static int lab_main_node_at_ply(const AnalysisSession *session, int ply)
{
    int node = session->root;
    for (int i = 0; i < ply; ++i) {
        node = session->positions[node].first_child;
        if (node < 0) {
            return -1;
        }
    }
    return node;
}

static int lab_main_length(const AnalysisSession *session)
{
    int node = session->root;
    int length = 0;
    while (session->positions[node].first_child >= 0) {
        node = session->positions[node].first_child;
        ++length;
    }
    return length;
}

static void lab_move_text(Move move, char out[3])
{
    if (!move_to_notation(move, out)) {
        out[0] = '-';
        out[1] = '\0';
    }
}

static LabLayout lab_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    LabLayout layout = {0};
    const bool wide = width >= 1000.0f && height >= 680.0f;
    layout.wide = wide;

    const float margin = wide ? LAB_MARGIN : 12.0f;
    const float top_height = wide ? LAB_TOP_HEIGHT : 44.0f;
    const float bottom_height = wide ? LAB_BOTTOM_HEIGHT : 84.0f;

    layout.top = (Rectangle){margin, margin, width - margin * 2.0f,
                             top_height};
    layout.bottom = (Rectangle){margin, height - margin - bottom_height,
                                width - margin * 2.0f, bottom_height};

    const float content_top = margin + top_height + 12.0f;
    const float content_bottom = layout.bottom.y - 12.0f;
    const float content_height = content_bottom - content_top;

    if (wide) {
        float side = fminf(width * 0.50f, content_height);
        if (side < 260.0f) {
            side = 260.0f;
        }
        layout.board = (Rectangle){
            margin,
            content_top + (content_height - side) * 0.5f,
            side, side
        };
        layout.cell = floorf(side / (float)REVERSI_BOARD_SIZE);
        layout.board.width = layout.cell * (float)REVERSI_BOARD_SIZE;
        layout.board.height = layout.board.width;
        const float panel_x = layout.board.x + layout.board.width + 16.0f;
        layout.panel = (Rectangle){panel_x, content_top,
                                   width - margin - panel_x, content_height};
        layout.graph = (Rectangle){layout.bottom.x + 330.0f,
                                   layout.bottom.y + 12.0f,
                                   layout.bottom.width - 350.0f,
                                   layout.bottom.height - 24.0f};
    } else {
        float side = fminf(width - margin * 2.0f,
                           content_height * 0.56f);
        if (side < 200.0f) {
            side = 200.0f;
        }
        layout.cell = floorf(side / (float)REVERSI_BOARD_SIZE);
        layout.board = (Rectangle){
            (width - layout.cell * (float)REVERSI_BOARD_SIZE) * 0.5f,
            content_top,
            layout.cell * (float)REVERSI_BOARD_SIZE,
            layout.cell * (float)REVERSI_BOARD_SIZE
        };
        const float panel_y = layout.board.y + layout.board.height + 8.0f;
        layout.panel = (Rectangle){margin, panel_y,
                                   width - margin * 2.0f,
                                   content_bottom - panel_y};
        layout.graph = (Rectangle){layout.panel.x + 8.0f,
                                   layout.panel.y + layout.panel.height - 44.0f,
                                   layout.panel.width - 16.0f, 36.0f};
    }

    const float row_height = wide ? 38.0f : 28.0f;
    const float row_gap = wide ? 4.0f : 3.0f;
    const float rows_y = layout.panel.y + (wide ? 96.0f : 76.0f);
    for (int i = 0; i < AI_ANALYSIS_MAX_LINES; ++i) {
        layout.candidate[i] = (Rectangle){
            layout.panel.x + 10.0f,
            rows_y + (float)i * (row_height + row_gap),
            layout.panel.width - 20.0f,
            row_height
        };
    }
    return layout;
}

static Rectangle lab_edit_strip(const LabLayout *layout)
{
    return (Rectangle){
        layout->bottom.x,
        layout->bottom.y - 44.0f,
        layout->bottom.width,
        38.0f
    };
}

static void lab_button(LabView *view, int index, const char *label,
                       bool primary, Rectangle bounds, bool enabled)
{
    UiButton *button = &view->buttons[index];
    button->label = label;
    button->primary = primary;
    button->enabled = enabled;
    ui_button_set_bounds(button, bounds);
}

static void lab_cancel_analysis(LabView *view, AiWorker *worker)
{
    if (view->analyzing && worker != NULL) {
        ai_worker_cancel(worker);
        AiAnalysis discarded;
        (void)ai_worker_take_analysis(worker, &discarded);
    }
    view->analyzing = false;
    view->batch_running = false;
}

static void lab_position_changed(LabView *view, AiWorker *worker)
{
    ++view->position_generation;
    view->has_analysis = false;
    view->selected_line = -1;
    lab_cancel_analysis(view, worker);
}

static bool lab_start_job(LabView *view, AiWorker *worker,
                          const GameState *state, int batch_index)
{
    if (worker == NULL || ai_worker_is_active(worker)) {
        return false;
    }
    AiConfig config = ai_default_config(AI_DIFFICULTY_HARD, 0x5EEDu);
    config.max_depth = 8;
    config.time_budget_ms = 1500;
    if (!ai_worker_start_analysis(worker, state, &config,
                                  AI_ANALYSIS_MAX_LINES)) {
        return false;
    }
    view->analyzing = true;
    view->requested_generation = view->position_generation;
    view->pending_batch_index = batch_index;
    return true;
}

static void lab_start_batch(LabView *view, AiWorker *worker)
{
    int nodes[ANALYSIS_MAX_PLIES + 1];
    int count = 1;
    nodes[0] = view->session.root;
    int node = view->session.root;
    while (view->session.positions[node].first_child >= 0
           && count <= ANALYSIS_MAX_PLIES) {
        node = view->session.positions[node].first_child;
        nodes[count++] = node;
    }
    memcpy(view->batch_nodes, nodes, (size_t)count * sizeof(nodes[0]));
    view->batch_count = count;
    view->batch_index = 0;
    view->batch_running = true;
    if (!lab_start_job(view, worker,
                       &view->session.positions[nodes[0]].state, 0)) {
        view->batch_running = false;
        lab_notice(view, "Could not start analysis");
    }
}

static void lab_poll_analysis(LabView *view, AiWorker *worker)
{
    if (!view->analyzing || worker == NULL) {
        return;
    }
    const AiWorkerStatus status = ai_worker_status(worker);
    if (status == AI_WORKER_RUNNING) {
        return;
    }

    AiAnalysis result;
    const bool ok = status == AI_WORKER_READY
        && ai_worker_take_analysis(worker, &result);
    view->analyzing = false;
    if (!ok) {
        AiAnalysis discarded;
        (void)ai_worker_take_analysis(worker, &discarded);
        view->batch_running = false;
        lab_notice(view, "Analysis cancelled or failed");
        return;
    }
    if (view->requested_generation != view->position_generation) {
        return;
    }

    if (view->batch_running) {
        const int index = view->pending_batch_index;
        if (index >= 0 && index < view->batch_count
            && result.line_count > 0) {
            const int node = view->batch_nodes[index];
            const Stone player =
                view->session.positions[node].state.current_player;
            view->score_valid[index] = true;
            view->score_values[index] =
                (float)lab_black_score(result.lines[0].score, player);
            view->best_valid[index] = true;
            view->best_moves[index] = result.lines[0].move;
            view->review_depth[index] = result.lines[0].depth;
            view->review_complete[index] = result.lines[0].complete;
            view->review_exact[index] = result.lines[0].exact;
        }
        view->batch_index = index + 1;
        if (view->batch_index < view->batch_count) {
            const int next = view->batch_nodes[view->batch_index];
            if (!lab_start_job(view, worker,
                               &view->session.positions[next].state,
                               view->batch_index)) {
                view->batch_running = false;
                lab_notice(view, "Could not continue analysis");
            }
        } else {
            view->batch_running = false;
            lab_notice(view, "Analysis complete");
        }
        return;
    }

    view->analysis = result;
    view->has_analysis = true;
    const AnalysisPosition *pos = analysis_current(&view->session);
    if (result.line_count > 0 && pos != NULL
        && pos->ply <= ANALYSIS_MAX_PLIES) {
        view->score_valid[pos->ply] = true;
        view->score_values[pos->ply] =
            (float)lab_black_score(result.lines[0].score,
                                   pos->state.current_player);
        view->best_valid[pos->ply] = true;
        view->best_moves[pos->ply] = result.lines[0].move;
        view->review_depth[pos->ply] = result.lines[0].depth;
        view->review_complete[pos->ply] = result.lines[0].complete;
        view->review_exact[pos->ply] = result.lines[0].exact;
    }
}

static void lab_navigate(LabView *view, AiWorker *worker, bool moved)
{
    if (moved) {
        lab_position_changed(view, worker);
    }
}

static void lab_handle_actions(LabView *view, AiWorker *worker,
                               const Match *match, LabAction *action)
{
    bool moved = false;

    if (view->clicked[LAB_BTN_BACK]) {
        *action = LAB_ACTION_BACK;
        return;
    }
    if (view->clicked[LAB_BTN_LOAD_GAME]) {
        if (view->has_match && match != NULL
            && match->history.count > 1) {
            moved = analysis_import_history(&view->session,
                                            &match->history);
            lab_notice(view, moved ? "Loaded game record"
                                   : "Could not load game record");
        } else if (view->has_match && match != NULL) {
            moved = analysis_load_state(&view->session, &match->state,
                                        ANALYSIS_POSITION_VALID);
            lab_notice(view, moved ? "Loaded current position"
                                   : "Current position is not playable");
        } else {
            lab_notice(view, "No game available");
        }
        if (moved) {
            lab_position_changed(view, worker);
        }
        return;
    }
    if (view->clicked[LAB_BTN_COPY]) {
        char record[ANALYSIS_MAX_RECORD_BYTES];
        const size_t written = analysis_record_write(
            &view->session, record, sizeof(record));
        if (written == 0) {
            lab_notice(view, "Record is too large or invalid");
        } else {
            SetClipboardText(record);
            lab_notice(view, "Record copied to clipboard");
        }
    }
    if (view->clicked[LAB_BTN_PASTE]) {
        const char *text = GetClipboardText();
        if (text == NULL || text[0] == '\0') {
            lab_notice(view, "Clipboard is empty");
        } else {
            AnalysisSession loaded;
            char error[128] = {0};
            if (analysis_record_read(&loaded, text, error,
                                     sizeof(error))) {
                view->session = loaded;
                moved = true;
                lab_notice(view, "Record imported");
            } else {
                lab_notice(view, error[0] != '\0' ? error
                                                  : "Invalid record");
            }
        }
        if (moved) {
            lab_position_changed(view, worker);
        }
    }
    if (view->clicked[LAB_BTN_FIRST]) {
        lab_navigate(view, worker, analysis_first(&view->session));
    }
    if (view->clicked[LAB_BTN_PREV]) {
        lab_navigate(view, worker, analysis_prev(&view->session));
    }
    if (view->clicked[LAB_BTN_NEXT]) {
        lab_navigate(view, worker, analysis_next(&view->session));
    }
    if (view->clicked[LAB_BTN_LAST]) {
        lab_navigate(view, worker, analysis_last(&view->session));
    }
    if (view->clicked[LAB_BTN_MAIN]) {
        lab_navigate(view, worker,
                     analysis_return_to_main(&view->session));
    }
    if (view->clicked[LAB_BTN_PASS]) {
        lab_navigate(view, worker, analysis_pass(&view->session));
    }
    if (view->clicked[LAB_BTN_PLAY]) {
        view->auto_play = !view->auto_play;
        view->auto_timer = 0.0f;
    }
    if (view->clicked[LAB_BTN_SPEED]) {
        view->speed_index = (view->speed_index + 1) % LAB_SPEED_COUNT;
    }
    if (view->clicked[LAB_BTN_ANALYZE]) {
        const AnalysisPosition *pos = analysis_current(&view->session);
        if (pos == NULL || !analysis_current_playable(&view->session)) {
            lab_notice(view, "No legal move to analyse");
        } else if (!lab_start_job(view, worker, &pos->state, -1)) {
            lab_notice(view, "Analysis worker is busy");
        }
    }
    if (view->clicked[LAB_BTN_STOP]) {
        lab_cancel_analysis(view, worker);
        lab_notice(view, "Analysis stopped");
    }
    if (view->clicked[LAB_BTN_ANALYZE_ALL]) {
        if (view->analyzing) {
            lab_notice(view, "Analysis is already running");
        } else {
            lab_start_batch(view, worker);
        }
    }
    if (view->clicked[LAB_BTN_EDIT]) {
        view->edit_mode = !view->edit_mode;
    }
    if (view->clicked[LAB_BTN_BLACK]) {
        view->edit_stone = STONE_BLACK;
    }
    if (view->clicked[LAB_BTN_WHITE]) {
        view->edit_stone = STONE_WHITE;
    }
    if (view->clicked[LAB_BTN_ERASE]) {
        view->edit_stone = STONE_EMPTY;
    }
    if (view->clicked[LAB_BTN_CLEAR]) {
        lab_navigate(view, worker, analysis_clear_board(&view->session));
    }
    if (view->clicked[LAB_BTN_STANDARD]) {
        lab_navigate(view, worker, analysis_set_standard(&view->session));
    }
    if (view->clicked[LAB_BTN_TURN]) {
        const AnalysisPosition *pos = analysis_current(&view->session);
        if (pos != NULL) {
            lab_navigate(view, worker, analysis_set_turn(
                &view->session,
                stone_opponent(pos->state.current_player)));
        }
    }
    if (view->clicked[LAB_BTN_SWAP]) {
        lab_navigate(view, worker, analysis_swap_colors(&view->session));
    }
    if (view->clicked[LAB_BTN_DONE]) {
        view->edit_mode = false;
    }
}

void lab_view_init(LabView *view)
{
    if (view == NULL) {
        return;
    }
    memset(view, 0, sizeof(*view));
    analysis_init(&view->session);
    view->edit_stone = STONE_BLACK;
    view->speed_index = 1;
    view->selected_line = -1;
    view->pending_batch_index = -1;
    for (int i = 0; i < LAB_BTN_COUNT; ++i) {
        ui_button_init(&view->buttons[i], "", false);
    }
}

void lab_view_enter(LabView *view, const Match *match, bool has_match)
{
    if (view == NULL) {
        return;
    }
    analysis_init(&view->session);
    view->has_match = has_match;
    if (has_match && match != NULL && match->history.count > 1) {
        if (!analysis_import_history(&view->session, &match->history)) {
            analysis_init(&view->session);
        }
    }

    memset(view->score_valid, 0, sizeof(view->score_valid));
    memset(view->score_values, 0, sizeof(view->score_values));
    memset(view->marks, 0, sizeof(view->marks));
    memset(view->best_valid, 0, sizeof(view->best_valid));
    memset(view->review_complete, 0, sizeof(view->review_complete));
    memset(view->review_exact, 0, sizeof(view->review_exact));
    memset(&view->analysis, 0, sizeof(view->analysis));
    view->has_analysis = false;
    view->analyzing = false;
    view->batch_running = false;
    view->batch_count = 0;
    view->batch_index = 0;
    view->edit_mode = false;
    view->edit_stone = STONE_BLACK;
    view->auto_play = false;
    view->auto_timer = 0.0f;
    view->selected_line = -1;
    ++view->position_generation;
    view->notice[0] = '\0';
    view->notice_timer = 0.0f;
}

void lab_view_leave(LabView *view, AiWorker *worker)
{
    if (view == NULL) {
        return;
    }
    lab_cancel_analysis(view, worker);
    if (worker != NULL) {
        ai_worker_join(worker);
    }
}

static void lab_prepare_buttons(LabView *view)
{
    LabLayout *layout = &view->layout;
    *layout = lab_layout();

    const float gap = 6.0f;
    const bool wide = layout->wide;
    const float top_button_height = 30.0f;
    const float top_count = 4.0f;
    const float top_width = wide ? 104.0f
        : fminf(104.0f, (layout->top.width - gap * 3.0f) / top_count);
    const float top_y = layout->top.y
        + (layout->top.height - top_button_height) * 0.5f;
    lab_button(view, LAB_BTN_BACK, "< Menu",
               false,
               (Rectangle){layout->top.x, top_y, top_width,
                           top_button_height}, true);
    lab_button(view, LAB_BTN_LOAD_GAME, "Load game", false,
               (Rectangle){layout->top.x + (top_width + gap), top_y,
                           top_width, top_button_height},
               view->has_match);
    lab_button(view, LAB_BTN_COPY, "Copy record", false,
               (Rectangle){layout->top.x + (top_width + gap) * 2.0f,
                           top_y, top_width, top_button_height}, true);
    lab_button(view, LAB_BTN_PASTE, "Paste record", false,
               (Rectangle){layout->top.x + (top_width + gap) * 3.0f,
                           top_y, top_width, top_button_height}, true);

    const float bottom_y = layout->bottom.y + 8.0f;
    const float bottom_gap = wide ? 6.0f : 4.0f;
    const float nav_width = wide ? 40.0f : 28.0f;
    const float nav_height = wide ? 34.0f : 30.0f;
    float x = layout->bottom.x + 6.0f;
    lab_button(view, LAB_BTN_FIRST, "|<", false,
               (Rectangle){x, bottom_y, nav_width, nav_height}, true);
    x += nav_width + bottom_gap;
    lab_button(view, LAB_BTN_PREV, "<", false,
               (Rectangle){x, bottom_y, nav_width, nav_height}, true);
    x += nav_width + bottom_gap;
    lab_button(view, LAB_BTN_NEXT, ">", false,
               (Rectangle){x, bottom_y, nav_width, nav_height}, true);
    x += nav_width + bottom_gap;
    lab_button(view, LAB_BTN_LAST, ">|", false,
               (Rectangle){x, bottom_y, nav_width, nav_height}, true);
    x += nav_width + bottom_gap;
    const float play_width = wide ? 64.0f : 52.0f;
    lab_button(view, LAB_BTN_PLAY, view->auto_play ? "Pause" : "Play",
               false, (Rectangle){x, bottom_y, play_width, nav_height},
               true);
    x += play_width + bottom_gap;
    static char speed_label[24];
    snprintf(speed_label, sizeof(speed_label), "Speed %.1fs",
             (double)LAB_SPEEDS[view->speed_index]);
    const float speed_width = wide ? 92.0f : 72.0f;
    lab_button(view, LAB_BTN_SPEED, speed_label, false,
               (Rectangle){x, bottom_y, speed_width, nav_height}, true);
    x += speed_width + bottom_gap;
    const AnalysisPosition *current = analysis_current(&view->session);
    lab_button(view, LAB_BTN_PASS, "Pass", false,
               (Rectangle){x, bottom_y, wide ? 56.0f : 40.0f, nav_height},
               current != NULL && game_must_pass(&current->state));

    const float panel_x = layout->panel.x + 10.0f;
    const float panel_w = layout->panel.width - 20.0f;
    const float panel_y = layout->panel.y + 8.0f;
    const float row_h = wide ? 32.0f : 28.0f;
    const float third = (panel_w - gap * 2.0f) / 3.0f;
    lab_button(view, LAB_BTN_ANALYZE, "Analyze", true,
               (Rectangle){panel_x, panel_y, third, row_h},
               !view->analyzing && !view->batch_running);
    lab_button(view, LAB_BTN_STOP, "Stop", false,
               (Rectangle){panel_x + third + gap, panel_y, third, row_h},
               view->analyzing || view->batch_running);
    lab_button(view, LAB_BTN_ANALYZE_ALL, "Analyze all", false,
               (Rectangle){panel_x + (third + gap) * 2.0f, panel_y, third,
                           row_h},
               !view->analyzing && !view->batch_running);
    const float half = (panel_w - gap) / 2.0f;
    lab_button(view, LAB_BTN_MAIN, "Main line", false,
               (Rectangle){panel_x, panel_y + row_h + gap, half, row_h},
               true);
    lab_button(view, LAB_BTN_EDIT,
               view->edit_mode ? "Editing" : "Edit board", false,
               (Rectangle){panel_x + half + gap, panel_y + row_h + gap,
                           half, row_h}, true);

    const Rectangle strip = lab_edit_strip(layout);
    const float edit_w = (strip.width - gap * 7.0f) / 8.0f;
    const float edit_y = strip.y + 3.0f;
    const float edit_h = strip.height - 6.0f;
    for (int i = 0; i < 8; ++i) {
        lab_button(view, LAB_BTN_BLACK + i, "", false,
                   (Rectangle){strip.x + (edit_w + gap) * (float)i, edit_y,
                               edit_w, edit_h},
                   view->edit_mode);
    }
    view->buttons[LAB_BTN_BLACK].label =
        view->edit_stone == STONE_BLACK ? "[B]" : "B";
    view->buttons[LAB_BTN_WHITE].label =
        view->edit_stone == STONE_WHITE ? "[W]" : "W";
    view->buttons[LAB_BTN_ERASE].label =
        view->edit_stone == STONE_EMPTY ? "[Er]" : "Er";
    view->buttons[LAB_BTN_CLEAR].label = "Clear";
    view->buttons[LAB_BTN_STANDARD].label = "Std";
    view->buttons[LAB_BTN_TURN].label = "Turn";
    view->buttons[LAB_BTN_SWAP].label = "Swap";
    view->buttons[LAB_BTN_DONE].label = "Done";

}

LabAction lab_view_update(LabView *view, const UiInput *input, float dt,
                          AiWorker *worker, const Match *match,
                          bool has_match)
{
    if (view == NULL || input == NULL) {
        return LAB_ACTION_NONE;
    }
    view->has_match = has_match;
    if (view->notice_timer > 0.0f) {
        view->notice_timer -= dt;
    }

    lab_prepare_buttons(view);
    memset(view->clicked, 0, sizeof(view->clicked));
    for (int i = 0; i < LAB_BTN_COUNT; ++i) {
        if (view->buttons[i].enabled
            && ui_button_update(&view->buttons[i], input, true, dt)) {
            view->clicked[i] = true;
        }
    }

    LabAction action = LAB_ACTION_NONE;
    lab_handle_actions(view, worker, match, &action);
    if (action != LAB_ACTION_NONE) {
        return action;
    }

    /* Board clicks: place a move in play mode, edit a cell in edit mode. */
    if (input->mouse_pressed
        && CheckCollisionPointRec(input->mouse, view->layout.board)) {
        int row = -1;
        int col = -1;
        if (board_view_hit_test(view->layout.board, view->layout.cell,
                                input->mouse, &row, &col)) {
            if (view->edit_mode) {
                if (analysis_edit_cell(&view->session, row, col,
                                       view->edit_stone)) {
                    lab_position_changed(view, worker);
                    lab_notice(view, "Custom position");
                }
            } else {
                const Move move = {row, col};
                if (analysis_play_move(&view->session, move)) {
                    lab_position_changed(view, worker);
                } else if (analysis_pass(&view->session)) {
                    lab_position_changed(view, worker);
                }
            }
        }
    }

    /* Candidate rows play the suggested move. */
    for (int i = 0; i < AI_ANALYSIS_MAX_LINES; ++i) {
        if (view->has_analysis && i < view->analysis.line_count
            && input->mouse_pressed
            && CheckCollisionPointRec(input->mouse,
                                      view->layout.candidate[i])) {
            view->selected_line = i;
            if (analysis_play_move(&view->session,
                                   view->analysis.lines[i].move)) {
                lab_position_changed(view, worker);
            }
        }
    }

    if (input->left_pressed) {
        lab_navigate(view, worker, analysis_prev(&view->session));
    }
    if (input->right_pressed) {
        lab_navigate(view, worker, analysis_next(&view->session));
    }
    if (input->enter_pressed) {
        view->auto_play = !view->auto_play;
        view->auto_timer = 0.0f;
    }
    if (input->menu_pressed) {
        const AnalysisPosition *pos = analysis_current(&view->session);
        if (pos != NULL && pos->ply <= ANALYSIS_MAX_PLIES) {
            view->marks[pos->ply] = !view->marks[pos->ply];
        }
    }
    if (input->up_pressed || input->down_pressed) {
        lab_navigate(view, worker,
                     analysis_next_variation(&view->session));
    }
    if (view->layout.wide && input->mouse_pressed
        && CheckCollisionPointRec(input->mouse, view->layout.graph)) {
        const int length = lab_main_length(&view->session);
        if (length > 0) {
            const Rectangle rect = view->layout.graph;
            float t = (input->mouse.x - (rect.x + 8.0f))
                / (rect.width - 16.0f);
            if (t < 0.0f) {
                t = 0.0f;
            }
            if (t > 1.0f) {
                t = 1.0f;
            }
            const int ply = (int)lroundf(t * (float)length);
            const int node = lab_main_node_at_ply(&view->session, ply);
            if (node >= 0 && node != view->session.current
                && analysis_goto(&view->session, node)) {
                lab_position_changed(view, worker);
            }
        }
    }
    if (input->escape_pressed) {
        return LAB_ACTION_BACK;
    }

    if (view->auto_play) {
        view->auto_timer += dt;
        if (view->auto_timer >= LAB_SPEEDS[view->speed_index]) {
            view->auto_timer = 0.0f;
            if (!analysis_next(&view->session)) {
                view->auto_play = false;
            } else {
                lab_position_changed(view, worker);
            }
        }
    }

    lab_poll_analysis(view, worker);
    return LAB_ACTION_NONE;
}

static void lab_draw_candidates(const LabView *view, const UiFonts *fonts)
{
    const LabLayout *layout = &view->layout;
    if (view->has_analysis) {
        for (int i = 0; i < view->analysis.line_count; ++i) {
            const AiAnalysisLine *line = &view->analysis.lines[i];
            const Rectangle row = layout->candidate[i];
            const bool selected = i == view->selected_line;
            ui_draw_panel(row, REVERSI_UI_RADIUS_SM,
                          selected ? REVERSI_UI_COLOR_SURFACE_HOVER
                                   : REVERSI_UI_COLOR_SURFACE_ALT,
                          selected ? REVERSI_UI_COLOR_ACCENT
                                   : REVERSI_UI_COLOR_BORDER);

            char move[3];
            lab_move_text(line->move, move);
            char text[128];
            int used = snprintf(text, sizeof(text), "%d. %s  %+d  d%d%s%s",
                                i + 1, move, line->score, line->depth,
                                line->complete ? "" : " (partial)",
                                line->exact ? " exact" : "");
            if (used < 0 || (size_t)used >= sizeof(text)) {
                text[0] = '\0';
            }
            for (int j = 0; j < line->pv_length && used > 0
                 && (size_t)used + 4 < sizeof(text); ++j) {
                char pv[3];
                lab_move_text(line->pv[j], pv);
                used += snprintf(text + used, sizeof(text) - (size_t)used,
                                 " %s", pv);
            }
            ui_draw_text_clipped(fonts, UI_TEXT_SEMIBOLD, text,
                                 (Rectangle){row.x + 10.0f, row.y, row.width - 20.0f,
                                             row.height},
                                 REVERSI_UI_FONT_SMALL,
                                 REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
        }
    } else {
        ui_draw_text_clipped(
            fonts, UI_TEXT_REGULAR,
            view->analyzing ? "Analyzing current position..."
                            : "No analysis yet. Press Analyze.",
            (Rectangle){layout->candidate[0].x,
                        layout->candidate[0].y,
                        layout->panel.width - 20.0f, 40.0f},
            REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED,
            UI_ALIGN_LEFT);
    }
}

static void lab_draw_review(const LabView *view, const UiFonts *fonts)
{
    if (!view->layout.wide) {
        return;
    }
    const AnalysisPosition *pos = analysis_current(&view->session);
    if (pos == NULL || pos->ply <= 0 || !pos->has_via) {
        return;
    }
    const int ply = pos->ply;
    if (!view->best_valid[ply - 1]) {
        return;
    }

    char played[8];
    if (pos->via.is_pass) {
        snprintf(played, sizeof(played), "pass");
    } else {
        char notation[3];
        lab_move_text(pos->via.move, notation);
        snprintf(played, sizeof(played), "%s", notation);
    }
    char best[3];
    lab_move_text(view->best_moves[ply - 1], best);

    const float x = view->layout.panel.x + 12.0f;
    const float y = view->layout.candidate[AI_ANALYSIS_MAX_LINES - 1].y
        + view->layout.candidate[AI_ANALYSIS_MAX_LINES - 1].height + 6.0f;
    const float width = view->layout.panel.width - 24.0f;

    char line[192];
    if (view->score_valid[ply] && view->score_valid[ply - 1]) {
        const int delta = (int)lroundf(view->score_values[ply]
                                       - view->score_values[ply - 1]);
        const bool matched = !pos->via.is_pass
            && pos->via.move.row == view->best_moves[ply - 1].row
            && pos->via.move.col == view->best_moves[ply - 1].col;
        snprintf(line, sizeof(line),
                 "Review: played %s, best %s%s, eval change %+d, d%d %s",
                 played, best, matched ? " (matched)" : "", delta,
                 view->review_depth[ply - 1],
                 view->review_exact[ply - 1] ? "exact"
                     : view->review_complete[ply - 1] ? "complete"
                                                      : "partial");
    } else {
        snprintf(line, sizeof(line), "Review: played %s, best %s", played,
                 best);
    }
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 16.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_LEFT);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR,
                         "Best-play comparison, not a win probability.",
                         (Rectangle){x, y + 15.0f, width, 16.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_LEFT);
}

static void lab_draw_evaluation(const LabView *view, const UiFonts *fonts)
{
    const AnalysisPosition *pos = analysis_current(&view->session);
    if (pos == NULL) {
        return;
    }
    AiEvaluation side;
    ai_evaluate(&pos->state, &side);
    AiEvaluation black;
    ai_evaluation_to_black(&side, pos->state.current_player, &black);

    const float x = view->layout.panel.x + 12.0f;
    float y = view->layout.candidate[AI_ANALYSIS_MAX_LINES - 1].y
        + view->layout.candidate[AI_ANALYSIS_MAX_LINES - 1].height
        + (view->layout.wide ? 44.0f : 10.0f);
    const float width = view->layout.panel.width - 24.0f;

    char line[128];
    snprintf(line, sizeof(line), "Evaluation (Black +): %+d", black.total);
    ui_draw_text_clipped(fonts, UI_TEXT_SEMIBOLD, line,
                         (Rectangle){x, y, width, 22.0f},
                         REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_LEFT);
    y += 24.0f;
    if (!view->layout.wide) {
        return;
    }

    snprintf(line, sizeof(line), "Corners %+d   Mobility %+d",
             black.corners, black.mobility);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_LEFT);
    y += 17.0f;
    snprintf(line, sizeof(line), "Potential %+d   Stable %+d",
             black.potential_mobility, black.stability);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_LEFT);
    y += 17.0f;
    snprintf(line, sizeof(line), "Frontier %+d   Position %+d",
             black.frontier, black.positional);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_LEFT);
    y += 17.0f;
    snprintf(line, sizeof(line), "Discs %+d   Empty %d",
             black.discs, black.empty_cells);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
                         UI_ALIGN_LEFT);
    y += 17.0f;

    const char *phase = side.phase == AI_PHASE_OPENING ? "Opening"
        : side.phase == AI_PHASE_MIDGAME ? "Midgame" : "Endgame";
    snprintf(line, sizeof(line), "Phase: %s%s", phase,
             view->has_analysis && view->analysis.exact
                 ? "   Exact endgame score" : "");
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, line,
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_LEFT);
    y += 18.0f;
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR,
                         "Heuristic scores are not win probabilities.",
                         (Rectangle){x, y, width, 18.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_LEFT);
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR,
                         "M mark · Up/Down branch · click graph to jump",
                         (Rectangle){x, y + 16.0f, width, 16.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_LEFT);
}

static void lab_draw_graph(const LabView *view, const UiFonts *fonts)
{
    const Rectangle rect = view->layout.graph;
    if (rect.width < 80.0f || rect.height < 20.0f) {
        return;
    }
    ui_draw_panel(rect, REVERSI_UI_RADIUS_SM,
                  REVERSI_UI_COLOR_SURFACE_ALT, REVERSI_UI_COLOR_BORDER);
    const int length = lab_main_length(&view->session);
    if (length <= 0) {
        return;
    }
    const float mid = rect.y + rect.height * 0.5f;
    DrawLineEx((Vector2){rect.x + 8.0f, mid},
               (Vector2){rect.x + rect.width - 8.0f, mid}, 1.0f,
               REVERSI_UI_COLOR_BORDER);

    const float step = (rect.width - 16.0f) / (float)length;
    Vector2 previous = {0};
    bool has_previous = false;
    for (int ply = 0; ply <= length; ++ply) {
        if (!view->score_valid[ply]) {
            continue;
        }
        float value = view->score_values[ply] / 2000.0f;
        if (value > 1.0f) {
            value = 1.0f;
        }
        if (value < -1.0f) {
            value = -1.0f;
        }
        const Vector2 point = {
            rect.x + 8.0f + step * (float)ply,
            mid - value * (rect.height * 0.42f)
        };
        if (has_previous) {
            DrawLineEx(previous, point, 2.0f, REVERSI_UI_COLOR_ACCENT);
        }
        DrawCircleV(point, 2.5f, REVERSI_UI_COLOR_ACCENT);
        previous = point;
        has_previous = true;
    }

    for (int ply = 0; ply <= length; ++ply) {
        if (view->marks[ply]) {
            const float x = rect.x + 8.0f + step * (float)ply;
            DrawRectangle((int)x - 2, (int)(rect.y + rect.height - 6.0f),
                          4, 4, REVERSI_UI_COLOR_HINT);
        }
    }

    const AnalysisPosition *pos = analysis_current(&view->session);
    if (pos != NULL && pos->ply <= length) {
        const float x = rect.x + 8.0f + step * (float)pos->ply;
        DrawLineEx((Vector2){x, rect.y + 4.0f},
                   (Vector2){x, rect.y + rect.height - 4.0f}, 1.5f,
                   REVERSI_UI_COLOR_HINT);
    }
    ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, "Eval curve (Black +)",
                         (Rectangle){rect.x + 8.0f, rect.y + 2.0f,
                                     rect.width - 16.0f, 14.0f},
                         REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT,
                         UI_ALIGN_LEFT);
}

void lab_view_draw(const LabView *view, const UiFonts *fonts)
{
    if (view == NULL || fonts == NULL) {
        return;
    }
    ui_draw_background();

    LabLayout computed = view->layout;
    if (computed.board.width <= 0.0f) {
        computed = lab_layout();
    }
    const LabLayout *layout = &computed;
    ui_draw_panel(layout->top, REVERSI_UI_RADIUS_MD,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);
    if (layout->wide) {
        ui_draw_text_clipped(fonts, UI_TEXT_SEMIBOLD, "Reversi Lab",
                             (Rectangle){layout->top.x + 8.0f,
                                         layout->top.y,
                                         layout->top.width - 16.0f,
                                         layout->top.height},
                             REVERSI_UI_FONT_HEADING,
                             REVERSI_UI_COLOR_TEXT, UI_ALIGN_CENTER);
    }

    const AnalysisPosition *pos = analysis_current(&view->session);
    if (pos != NULL) {
        board_view_draw(&pos->state, layout->board, layout->cell, fonts,
                        NULL, false, false, (Move){0, 0}, false, 0.0f,
                        -1, -1, false, view->edit_mode, false);

        /* Candidate rings on the board. */
        if (view->has_analysis) {
            for (int i = view->analysis.line_count - 1; i >= 0; --i) {
                const Move move = view->analysis.lines[i].move;
                if (!board_in_bounds(move.row, move.col)) {
                    continue;
                }
                const Vector2 center = {
                    layout->board.x + ((float)move.col + 0.5f) * layout->cell,
                    layout->board.y + ((float)move.row + 0.5f) * layout->cell
                };
                const float radius = layout->cell * (i == 0 ? 0.42f : 0.32f);
                ui_draw_ring(center, radius, fmaxf(2.0f, layout->cell * 0.04f),
                             ui_color_alpha(REVERSI_UI_COLOR_ACCENT,
                                            i == 0 ? 0.95f : 0.55f));
            }
        }
    }

    ui_draw_panel(layout->panel, REVERSI_UI_RADIUS_MD,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);

    char status[192];
    if (view->analyzing) {
        snprintf(status, sizeof(status), "Analyzing...%s",
                 view->batch_running ? " (batch)" : "");
    } else if (view->has_analysis) {
        snprintf(status, sizeof(status), "Depth %d%s%s",
                 view->analysis.depth,
                 view->analysis.complete ? ", complete" : ", partial",
                 view->analysis.exact ? ", exact endgame" : "");
    } else {
        snprintf(status, sizeof(status), "Idle");
    }
    if (pos != NULL) {
        const size_t used = strlen(status);
        snprintf(status + used, sizeof(status) - used, "   Ply %d/%d",
                 pos->ply, lab_main_length(&view->session));
    }
    if (layout->wide) {
        ui_draw_text_clipped(
            fonts, UI_TEXT_SEMIBOLD, status,
            (Rectangle){layout->panel.x + 12.0f,
                        layout->panel.y + 68.0f,
                        layout->panel.width - 24.0f, 18.0f},
            REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
            UI_ALIGN_LEFT);
    }

    lab_draw_candidates(view, fonts);
    lab_draw_review(view, fonts);
    lab_draw_evaluation(view, fonts);

    ui_draw_panel(layout->bottom, REVERSI_UI_RADIUS_MD,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER);
    if (layout->wide) {
        lab_draw_graph(view, fonts);
    }

    if (view->edit_mode) {
        const Rectangle strip = lab_edit_strip(layout);
        ui_draw_panel(strip, REVERSI_UI_RADIUS_SM,
                      REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_ACCENT);
        ui_draw_text_clipped(
            fonts, UI_TEXT_SEMIBOLD,
            "Edit mode: click a cell to place or erase.",
            (Rectangle){layout->board.x, layout->board.y - 20.0f,
                        layout->board.width, 18.0f},
            REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_MUTED,
            UI_ALIGN_CENTER);
    }

    for (int i = 0; i < LAB_BTN_COUNT; ++i) {
        if (i < LAB_BTN_BLACK || view->edit_mode) {
            ui_button_draw(&view->buttons[i], fonts, false);
        }
    }

    if (view->notice_timer > 0.0f && view->notice[0] != '\0') {
        ui_draw_text_clipped(
            fonts, UI_TEXT_REGULAR, view->notice,
            (Rectangle){layout->bottom.x, layout->bottom.y - 62.0f,
                        layout->bottom.width, 18.0f},
            REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_ACCENT,
            UI_ALIGN_LEFT);
    }
}
