#include "analysis/analysis.h"

#include "core/rules.h"

#include <stdlib.h>
#include <string.h>

static void reset_root(AnalysisSession *session, const GameState *state,
                       AnalysisPositionKind kind)
{
    memset(session, 0, sizeof(*session));
    session->count = 1;
    session->root = 0;
    session->current = 0;

    AnalysisPosition *root = &session->positions[0];
    root->state = *state;
    root->kind = kind;
    root->parent = -1;
    root->first_child = -1;
    root->next_sibling = -1;
    root->ply = 0;
    root->has_via = false;
}

static GameResult result_from_counts(const Board *board)
{
    const int black = board_count(board, STONE_BLACK);
    const int white = board_count(board, STONE_WHITE);
    if (black > white) {
        return GAME_RESULT_BLACK_WIN;
    }
    if (white > black) {
        return GAME_RESULT_WHITE_WIN;
    }
    return GAME_RESULT_DRAW;
}

/* Keeps a custom board's result consistent with the rules without claiming
   a history: both sides stuck means the position is terminal. */
static void refresh_custom_result(GameState *state)
{
    if (board_is_full(&state->board) || (!stone_is_player(state->current_player))
        || (rules_must_pass(&state->board, state->current_player)
            && rules_must_pass(&state->board,
                               stone_opponent(state->current_player)))) {
        state->result = result_from_counts(&state->board);
        return;
    }
    state->result = GAME_RESULT_ONGOING;
}

static int last_child(const AnalysisSession *session, int parent)
{
    int child = session->positions[parent].first_child;
    if (child < 0) {
        return -1;
    }
    while (session->positions[child].next_sibling >= 0) {
        child = session->positions[child].next_sibling;
    }
    return child;
}

static int add_child(AnalysisSession *session, int parent,
                     const GameState *state, AnalysisMove via)
{
    if (session->count >= ANALYSIS_MAX_POSITIONS) {
        return -1;
    }
    const AnalysisPosition *parent_pos = &session->positions[parent];
    const int index = session->count++;
    AnalysisPosition *node = &session->positions[index];
    memset(node, 0, sizeof(*node));
    node->state = *state;
    node->kind = parent_pos->kind;
    node->parent = parent;
    node->first_child = -1;
    node->next_sibling = -1;
    node->ply = parent_pos->ply + 1;
    node->via = via;
    node->has_via = true;

    const int tail = last_child(session, parent);
    if (tail < 0) {
        session->positions[parent].first_child = index;
    } else {
        session->positions[tail].next_sibling = index;
    }
    return index;
}

static int find_child(AnalysisSession *session, int parent, Move move,
                      bool is_pass)
{
    for (int c = session->positions[parent].first_child; c >= 0;
         c = session->positions[c].next_sibling) {
        const AnalysisMove *via = &session->positions[c].via;
        if (via->is_pass == is_pass
            && (is_pass
                || (via->move.row == move.row
                    && via->move.col == move.col))) {
            return c;
        }
    }
    return -1;
}

void analysis_init(AnalysisSession *session)
{
    if (session == NULL) {
        return;
    }
    GameState state;
    game_init(&state);
    reset_root(session, &state, ANALYSIS_POSITION_VALID);
}

void analysis_reset_custom(AnalysisSession *session, Stone to_move)
{
    if (session == NULL || !stone_is_player(to_move)) {
        return;
    }
    GameState state;
    memset(&state, 0, sizeof(state));
    board_clear(&state.board);
    state.current_player = to_move;
    state.result = GAME_RESULT_ONGOING;
    refresh_custom_result(&state);
    reset_root(session, &state, ANALYSIS_POSITION_CUSTOM);
}

bool analysis_load_state(AnalysisSession *session, const GameState *state,
                         AnalysisPositionKind kind)
{
    if (session == NULL || state == NULL || !board_is_valid(&state->board)
        || !stone_is_player(state->current_player)
        || (kind != ANALYSIS_POSITION_VALID
            && kind != ANALYSIS_POSITION_CUSTOM)) {
        return false;
    }
    GameState copy = *state;
    if (kind == ANALYSIS_POSITION_CUSTOM) {
        refresh_custom_result(&copy);
    } else if (copy.result != GAME_RESULT_ONGOING) {
        /* A finished game is a valid starting point for review. */
        if (!game_is_consistent(&copy)) {
            return false;
        }
    } else if (!game_is_consistent(&copy)) {
        return false;
    }
    reset_root(session, &copy, kind);
    return true;
}

bool analysis_import_history(AnalysisSession *session, const History *history)
{
    if (session == NULL || history == NULL || history->count <= 0
        || history->entries[0].has_move) {
        return false;
    }

    AnalysisSession *tmp = malloc(sizeof(*tmp));
    if (tmp == NULL) {
        return false;
    }
    const GameState *initial = &history->entries[0].state;
    if (!analysis_load_state(tmp, initial, ANALYSIS_POSITION_VALID)) {
        free(tmp);
        return false;
    }

    for (int i = 1; i < history->count; ++i) {
        const HistoryEntry *entry = &history->entries[i];
        if (!entry->has_move) {
            free(tmp);
            return false;
        }
        if (!analysis_play_move(tmp, entry->move)) {
            free(tmp);
            return false;
        }
    }

    *session = *tmp;
    free(tmp);
    return true;
}

const AnalysisPosition *analysis_current(const AnalysisSession *session)
{
    if (session == NULL || session->current < 0
        || session->current >= session->count) {
        return NULL;
    }
    return &session->positions[session->current];
}

const AnalysisPosition *analysis_position(const AnalysisSession *session,
                                          int index)
{
    if (session == NULL || index < 0 || index >= session->count) {
        return NULL;
    }
    return &session->positions[index];
}

bool analysis_current_playable(const AnalysisSession *session)
{
    const AnalysisPosition *pos = analysis_current(session);
    if (pos == NULL || game_is_over(&pos->state)) {
        return false;
    }
    return rules_count_legal_moves(&pos->state.board,
                                   pos->state.current_player) > 0
        || game_must_pass(&pos->state);
}

AnalysisPositionKind analysis_current_kind(const AnalysisSession *session)
{
    const AnalysisPosition *pos = analysis_current(session);
    return pos != NULL ? pos->kind : ANALYSIS_POSITION_CUSTOM;
}

AnalysisResult analysis_current_result(const AnalysisSession *session)
{
    AnalysisResult result = {
        .result = GAME_RESULT_ONGOING,
        .black_discs = 0,
        .white_discs = 0,
        .terminal = false
    };
    const AnalysisPosition *pos = analysis_current(session);
    if (pos == NULL) {
        return result;
    }
    result.result = pos->state.result;
    result.black_discs = board_count(&pos->state.board, STONE_BLACK);
    result.white_discs = board_count(&pos->state.board, STONE_WHITE);
    result.terminal = game_is_over(&pos->state);
    return result;
}

bool analysis_play_move(AnalysisSession *session, Move move)
{
    AnalysisPosition *pos = (AnalysisPosition *)analysis_current(session);
    if (pos == NULL || game_is_over(&pos->state)
        || game_must_pass(&pos->state)
        || !rules_is_legal_move(&pos->state.board, pos->state.current_player,
                                move)) {
        return false;
    }

    const int existing = find_child(session, session->current, move, false);
    if (existing >= 0) {
        session->current = existing;
        return true;
    }

    const Stone mover = pos->state.current_player;
    GameState child = pos->state;
    if (!game_apply_move(&child, move)) {
        return false;
    }
    const bool opponent_passed =
        child.result == GAME_RESULT_ONGOING
        && child.current_player == mover;

    AnalysisMove via = {
        .move = move,
        .mover = mover,
        .is_pass = false,
        .opponent_passed = opponent_passed
    };
    const int index = add_child(session, session->current, &child, via);
    if (index < 0) {
        return false;
    }
    session->current = index;
    return true;
}

bool analysis_pass(AnalysisSession *session)
{
    AnalysisPosition *pos = (AnalysisPosition *)analysis_current(session);
    if (pos == NULL || !game_must_pass(&pos->state)) {
        return false;
    }

    const int existing = find_child(session, session->current, (Move){0, 0},
                                    true);
    if (existing >= 0) {
        session->current = existing;
        return true;
    }

    const Stone mover = pos->state.current_player;
    GameState child = pos->state;
    if (!game_pass(&child)) {
        return false;
    }
    AnalysisMove via = {
        .move = {0, 0},
        .mover = mover,
        .is_pass = true,
        .opponent_passed = false
    };
    const int index = add_child(session, session->current, &child, via);
    if (index < 0) {
        return false;
    }
    session->current = index;
    return true;
}

bool analysis_goto(AnalysisSession *session, int index)
{
    if (session == NULL || index < 0 || index >= session->count) {
        return false;
    }
    session->current = index;
    return true;
}

bool analysis_first(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    session->current = session->root;
    return true;
}

bool analysis_prev(AnalysisSession *session)
{
    if (session == NULL || session->current == session->root) {
        return false;
    }
    session->current = session->positions[session->current].parent;
    return true;
}

bool analysis_next(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    const int child = session->positions[session->current].first_child;
    if (child < 0) {
        return false;
    }
    session->current = child;
    return true;
}

bool analysis_last(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    while (session->positions[session->current].first_child >= 0) {
        session->current = session->positions[session->current].first_child;
    }
    return true;
}

bool analysis_return_to_main(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    const int target_ply = session->positions[session->current].ply;
    int node = session->root;
    while (session->positions[node].ply < target_ply
           && session->positions[node].first_child >= 0) {
        node = session->positions[node].first_child;
    }
    session->current = node;
    return true;
}

int analysis_variation_count(const AnalysisSession *session)
{
    if (session == NULL) {
        return 0;
    }
    int count = 0;
    for (int c = session->positions[session->current].first_child; c >= 0;
         c = session->positions[c].next_sibling) {
        ++count;
    }
    return count;
}

bool analysis_variation_at(const AnalysisSession *session, int index,
                           AnalysisVariation *out)
{
    if (session == NULL || out == NULL || index < 0) {
        return false;
    }
    int child = session->positions[session->current].first_child;
    for (int i = 0; i < index && child >= 0; ++i) {
        child = session->positions[child].next_sibling;
    }
    if (child < 0) {
        return false;
    }
    int end = child;
    while (session->positions[end].first_child >= 0) {
        end = session->positions[end].first_child;
    }
    out->start = session->current;
    out->end = end;
    out->length = session->positions[end].ply
        - session->positions[session->current].ply;
    out->child_index = index;
    return true;
}

bool analysis_next_variation(AnalysisSession *session)
{
    if (session == NULL || session->current == session->root) {
        return false;
    }
    const int next = session->positions[session->current].next_sibling;
    if (next < 0) {
        return false;
    }
    session->current = next;
    return true;
}

/* Replaces the tree with the current board as a CUSTOM root. */
static void make_custom_root(AnalysisSession *session)
{
    GameState state = session->positions[session->current].state;
    if (game_is_over(&state)) {
        state.result = GAME_RESULT_ONGOING;
    }
    refresh_custom_result(&state);
    reset_root(session, &state, ANALYSIS_POSITION_CUSTOM);
}

bool analysis_edit_cell(AnalysisSession *session, int row, int col,
                        Stone stone)
{
    if (session == NULL || !board_in_bounds(row, col)
        || (stone != STONE_EMPTY && stone != STONE_BLACK
            && stone != STONE_WHITE)) {
        return false;
    }
    make_custom_root(session);
    AnalysisPosition *root = &session->positions[0];
    if (!board_set(&root->state.board, row, col, stone)) {
        return false;
    }
    refresh_custom_result(&root->state);
    return true;
}

bool analysis_clear_board(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    make_custom_root(session);
    AnalysisPosition *root = &session->positions[0];
    board_clear(&root->state.board);
    refresh_custom_result(&root->state);
    return true;
}

bool analysis_set_standard(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    analysis_init(session);
    return true;
}

bool analysis_set_turn(AnalysisSession *session, Stone player)
{
    if (session == NULL || !stone_is_player(player)) {
        return false;
    }
    make_custom_root(session);
    AnalysisPosition *root = &session->positions[0];
    root->state.current_player = player;
    refresh_custom_result(&root->state);
    return true;
}

bool analysis_swap_colors(AnalysisSession *session)
{
    if (session == NULL) {
        return false;
    }
    make_custom_root(session);
    AnalysisPosition *root = &session->positions[0];
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board_get(&root->state.board, row, col);
            if (stone == STONE_BLACK) {
                board_set(&root->state.board, row, col, STONE_WHITE);
            } else if (stone == STONE_WHITE) {
                board_set(&root->state.board, row, col, STONE_BLACK);
            }
        }
    }
    root->state.current_player =
        stone_opponent(root->state.current_player);
    refresh_custom_result(&root->state);
    return true;
}
