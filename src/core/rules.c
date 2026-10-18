#include "core/rules.h"

#include <stddef.h>

static const int DIRECTIONS[8][2] = {
    {-1, -1}, {-1, 0}, {-1, 1},
    { 0, -1},          { 0, 1},
    { 1, -1}, { 1, 0}, { 1, 1}
};

static bool player_is_valid(Stone player)
{
    return player == STONE_BLACK || player == STONE_WHITE;
}

/*
 * Counts opponent stones that would be flipped along one direction. Returns
 * 0 when the line is empty, leaves the board, or is not terminated by the
 * moving player's own stone.
 */
static int flips_in_direction(const Board *board, Stone player, Move move,
                              int row_step, int col_step)
{
    const Stone opponent = stone_opponent(player);
    int row = move.row + row_step;
    int col = move.col + col_step;
    int flips = 0;

    while (board_in_bounds(row, col)
           && board->cells[row][col] == opponent) {
        ++flips;
        row += row_step;
        col += col_step;
    }

    if (flips == 0 || !board_in_bounds(row, col)) {
        return 0;
    }
    if (board->cells[row][col] != player) {
        return 0;
    }
    return flips;
}

bool rules_is_legal_move(const Board *board, Stone player, Move move)
{
    if (board == NULL || !player_is_valid(player)) {
        return false;
    }
    if (!board_in_bounds(move.row, move.col)) {
        return false;
    }
    if (board->cells[move.row][move.col] != STONE_EMPTY) {
        return false;
    }

    for (int i = 0; i < 8; ++i) {
        if (flips_in_direction(board, player, move,
                               DIRECTIONS[i][0], DIRECTIONS[i][1]) > 0) {
            return true;
        }
    }
    return false;
}

int rules_generate_legal_moves(const Board *board, Stone player,
                               Move *moves, int capacity)
{
    if (board == NULL || !player_is_valid(player)
        || moves == NULL || capacity <= 0) {
        return 0;
    }

    int written = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board->cells[row][col] != STONE_EMPTY) {
                continue;
            }

            const Move move = {.row = row, .col = col};
            if (!rules_is_legal_move(board, player, move)) {
                continue;
            }

            moves[written] = move;
            ++written;
            if (written == capacity) {
                return written;
            }
        }
    }
    return written;
}

int rules_count_legal_moves(const Board *board, Stone player)
{
    if (board == NULL || !player_is_valid(player)) {
        return 0;
    }

    int count = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board->cells[row][col] != STONE_EMPTY) {
                continue;
            }

            const Move move = {.row = row, .col = col};
            if (rules_is_legal_move(board, player, move)) {
                ++count;
            }
        }
    }
    return count;
}

bool rules_must_pass(const Board *board, Stone player)
{
    if (board == NULL || !player_is_valid(player)) {
        return false;
    }
    return rules_count_legal_moves(board, player) == 0;
}

int rules_apply_move(Board *board, Stone player, Move move)
{
    if (board == NULL || !player_is_valid(player)) {
        return -1;
    }
    if (!board_in_bounds(move.row, move.col)
        || board->cells[move.row][move.col] != STONE_EMPTY) {
        return -1;
    }

    int flips[8] = {0};
    int total_flips = 0;

    for (int i = 0; i < 8; ++i) {
        flips[i] = flips_in_direction(board, player, move,
                                      DIRECTIONS[i][0], DIRECTIONS[i][1]);
        total_flips += flips[i];
    }

    if (total_flips == 0) {
        return -1;
    }

    board->cells[move.row][move.col] = player;

    for (int i = 0; i < 8; ++i) {
        int row = move.row + DIRECTIONS[i][0];
        int col = move.col + DIRECTIONS[i][1];

        for (int n = 0; n < flips[i]; ++n) {
            board->cells[row][col] = player;
            row += DIRECTIONS[i][0];
            col += DIRECTIONS[i][1];
        }
    }

    return total_flips;
}
