#include "core/board.h"

#include <stddef.h>
#include <string.h>

bool board_in_bounds(int row, int col)
{
    return row >= 0 && row < REVERSI_BOARD_SIZE
        && col >= 0 && col < REVERSI_BOARD_SIZE;
}

bool board_is_full(const Board *board)
{
    if (board == NULL) {
        return false;
    }

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board->cells[row][col] == STONE_EMPTY) {
                return false;
            }
        }
    }
    return true;
}

bool board_is_valid(const Board *board)
{
    if (board == NULL) {
        return false;
    }

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const Stone stone = board->cells[row][col];
            if (stone != STONE_EMPTY && !stone_is_player(stone)) {
                return false;
            }
        }
    }
    return true;
}

Stone board_get(const Board *board, int row, int col)
{
    if (board == NULL || !board_in_bounds(row, col)) {
        return STONE_EMPTY;
    }
    return board->cells[row][col];
}

bool board_set(Board *board, int row, int col, Stone stone)
{
    if (board == NULL || !board_in_bounds(row, col)) {
        return false;
    }
    if (stone != STONE_EMPTY && !stone_is_player(stone)) {
        return false;
    }

    board->cells[row][col] = stone;
    return true;
}

void board_clear(Board *board)
{
    if (board == NULL) {
        return;
    }

    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            board->cells[row][col] = STONE_EMPTY;
        }
    }
}

void board_init(Board *board)
{
    if (board == NULL) {
        return;
    }

    board_clear(board);

    /* Standard opening, mapped through row = '8' - rank. */
    board->cells[3][3] = STONE_BLACK; /* d5 */
    board->cells[3][4] = STONE_WHITE; /* e5 */
    board->cells[4][3] = STONE_WHITE; /* d4 */
    board->cells[4][4] = STONE_BLACK; /* e4 */
}

int board_count(const Board *board, Stone stone)
{
    if (board == NULL) {
        return 0;
    }

    int count = 0;
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            if (board->cells[row][col] == stone) {
                ++count;
            }
        }
    }
    return count;
}

Stone stone_opponent(Stone stone)
{
    if (stone == STONE_BLACK) {
        return STONE_WHITE;
    }
    if (stone == STONE_WHITE) {
        return STONE_BLACK;
    }
    return STONE_EMPTY;
}

bool stone_is_player(Stone stone)
{
    return stone == STONE_BLACK || stone == STONE_WHITE;
}

bool move_from_notation(const char *text, Move *out_move)
{
    if (text == NULL || out_move == NULL || strlen(text) != 2) {
        return false;
    }

    char file = text[0];
    const char rank = text[1];

    if (file >= 'A' && file <= 'H') {
        file = (char)(file - 'A' + 'a');
    }
    if (file < 'a' || file > 'h') {
        return false;
    }
    if (rank < '1' || rank > '8') {
        return false;
    }

    const Move move = {
        .row = '8' - rank,
        .col = file - 'a'
    };
    *out_move = move;
    return true;
}

bool move_to_notation(Move move, char out[3])
{
    if (out == NULL || !board_in_bounds(move.row, move.col)) {
        return false;
    }

    out[0] = (char)('a' + move.col);
    out[1] = (char)('8' - move.row);
    out[2] = '\0';
    return true;
}
