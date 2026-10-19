#include "ai/ai.h"
#include "core/game.h"
#include "core/rules.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BENCH_MAX_PLIES 200
#define BENCH_SEED 0x5EED1234ULL

typedef enum BenchKind {
    BENCH_RANDOM = 0,
    BENCH_GREEDY,
    BENCH_AI
} BenchKind;

typedef struct BenchPlayer {
    const char *name;
    BenchKind kind;
    AiConfig config;
    Stone color;
    uint64_t rng;
} BenchPlayer;

typedef struct BenchTotals {
    uint64_t nodes;
    uint64_t cutoffs;
    uint64_t tt_hits;
    uint64_t elapsed_ms;
    int ai_moves;
    int max_depth;
    int failures;
    int illegal;
    int unfinished;
} BenchTotals;

typedef enum BenchOutcome {
    BENCH_WIN = 0,
    BENCH_DRAW,
    BENCH_LOSS
} BenchOutcome;

static uint64_t next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int legal_moves(const GameState *state, Move *moves)
{
    return rules_generate_legal_moves(&state->board, state->current_player,
                                      moves, REVERSI_BOARD_CELL_COUNT);
}

static Move random_move(const GameState *state, uint64_t *rng)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = legal_moves(state, moves);
    return count > 0 ? moves[next_random(rng) % (uint64_t)count] : (Move){0, 0};
}

static Move greedy_move(const GameState *state)
{
    Move moves[REVERSI_BOARD_CELL_COUNT];
    const int count = legal_moves(state, moves);
    if (count <= 0) {
        return (Move){0, 0};
    }

    Move best = moves[0];
    int best_score = -1000000;
    for (int i = 0; i < count; ++i) {
        GameState child = *state;
        if (rules_apply_move(&child.board, state->current_player,
                             moves[i]) < 0) {
            continue;
        }
        child.current_player = stone_opponent(state->current_player);
        child.result = GAME_RESULT_ONGOING;
        AiEvaluation evaluation;
        ai_evaluate(&child, &evaluation);
        const int score = -evaluation.total;
        if (score > best_score) {
            best_score = score;
            best = moves[i];
        }
    }
    return best;
}

static bool ai_move(const GameState *state, BenchPlayer *player,
                    Move *out_move, BenchTotals *totals)
{
    AiStats stats;
    if (!ai_choose_move(state, &player->config, out_move, &stats)) {
        ++totals->failures;
        Move legal[REVERSI_BOARD_CELL_COUNT];
        const int count = legal_moves(state, legal);
        if (count <= 0) {
            return false;
        }
        *out_move = legal[0];
        return true;
    }

    totals->nodes += stats.nodes;
    totals->cutoffs += stats.cutoffs;
    totals->tt_hits += stats.tt_hits;
    totals->elapsed_ms += (uint64_t)stats.elapsed_ms;
    ++totals->ai_moves;
    if (stats.depth > totals->max_depth) {
        totals->max_depth = stats.depth;
    }
    return true;
}

static bool choose_move(const GameState *state, BenchPlayer *player,
                        Move *out_move, BenchTotals *totals)
{
    switch (player->kind) {
    case BENCH_RANDOM:
        *out_move = random_move(state, &player->rng);
        return true;
    case BENCH_GREEDY:
        *out_move = greedy_move(state);
        return true;
    case BENCH_AI:
    default:
        return ai_move(state, player, out_move, totals);
    }
}

static BenchOutcome play_game(BenchPlayer *black, BenchPlayer *white,
                              BenchTotals *totals)
{
    GameState state;
    game_init(&state);
    black->color = STONE_BLACK;
    white->color = STONE_WHITE;

    for (int ply = 0; ply < BENCH_MAX_PLIES
         && state.result == GAME_RESULT_ONGOING; ++ply) {
        if (game_must_pass(&state)) {
            if (!game_pass(&state)) {
                ++totals->illegal;
                break;
            }
            continue;
        }

        BenchPlayer *player = state.current_player == STONE_BLACK
            ? black : white;
        Move move = {0, 0};
        if (!choose_move(&state, player, &move, totals)) {
            ++totals->illegal;
            break;
        }
        if (!rules_is_legal_move(&state.board, state.current_player, move)) {
            ++totals->illegal;
            break;
        }
        if (!game_apply_move(&state, move)) {
            ++totals->illegal;
            break;
        }
    }

    if (state.result == GAME_RESULT_ONGOING) {
        ++totals->unfinished;
        return BENCH_DRAW;
    }
    if (state.result == GAME_RESULT_DRAW) {
        return BENCH_DRAW;
    }
    const bool black_wins = state.result == GAME_RESULT_BLACK_WIN;
    if (black_wins) {
        return BENCH_WIN;
    }
    return BENCH_LOSS;
}

static void run_matchup(const char *firstName, BenchKind first_kind,
                        const AiConfig *first_config,
                        const char *secondName, BenchKind second_kind,
                        const AiConfig *second_config, int games)
{
    int wins = 0;
    int draws = 0;
    int losses = 0;
    BenchTotals totals = {0};

    for (int game = 0; game < games; ++game) {
        BenchPlayer first = {
            .name = firstName,
            .kind = first_kind,
            .rng = BENCH_SEED + (uint64_t)game * 7919u + 1u
        };
        BenchPlayer second = {
            .name = secondName,
            .kind = second_kind,
            .rng = BENCH_SEED + (uint64_t)game * 104729u + 3u
        };
        if (first_config != NULL) {
            first.config = *first_config;
            first.config.seed = (unsigned)(BENCH_SEED + (uint64_t)game * 17u);
        }
        if (second_config != NULL) {
            second.config = *second_config;
            second.config.seed = (unsigned)(BENCH_SEED + (uint64_t)game * 31u);
        }

        BenchPlayer *black = (game % 2 == 0) ? &first : &second;
        BenchPlayer *white = (game % 2 == 0) ? &second : &first;
        const BenchOutcome outcome = play_game(black, white, &totals);

        if (outcome == BENCH_DRAW) {
            ++draws;
        } else if (black == &first) {
            if (outcome == BENCH_WIN) {
                ++wins;
            } else {
                ++losses;
            }
        } else {
            if (outcome == BENCH_WIN) {
                ++losses;
            } else {
                ++wins;
            }
        }
    }

    const double average_ms = totals.ai_moves > 0
        ? (double)totals.elapsed_ms / (double)totals.ai_moves : 0.0;
    const double cutoff_ratio = totals.nodes > 0
        ? (double)totals.cutoffs / (double)totals.nodes : 0.0;

    printf("%-8s vs %-8s  games=%d  W=%d D=%d L=%d  illegal=%d failures=%d "
           "unfinished=%d\n",
           firstName, secondName, games, wins, draws, losses,
           totals.illegal, totals.failures, totals.unfinished);
    printf("         nodes=%" PRIu64 " cutoffs=%" PRIu64 " (%.1f%%) "
           "tt_hits=%" PRIu64 " ai_moves=%d avg_ms=%.1f max_depth=%d\n",
           totals.nodes, totals.cutoffs, cutoff_ratio * 100.0,
           totals.tt_hits, totals.ai_moves, average_ms, totals.max_depth);
}

static void probe_position(const char *label, const GameState *state)
{
    printf("\n[%s] current=%s empty=%d\n", label,
           state->current_player == STONE_BLACK ? "Black" : "White",
           REVERSI_BOARD_CELL_COUNT
               - board_count(&state->board, STONE_BLACK)
               - board_count(&state->board, STONE_WHITE));

    const AiDifficulty difficulties[3] = {
        AI_DIFFICULTY_EASY, AI_DIFFICULTY_MEDIUM, AI_DIFFICULTY_HARD
    };
    for (int i = 0; i < 3; ++i) {
        AiConfig config = ai_default_config(difficulties[i], 1234);
        Move move = {0, 0};
        AiStats stats;
        const bool found = ai_choose_move(state, &config, &move, &stats);
        char notation[3] = "??";
        if (found) {
            (void)move_to_notation(move, notation);
        }
        const double cutoff_ratio = stats.nodes > 0
            ? (double)stats.cutoffs / (double)stats.nodes : 0.0;
        printf("  %-6s found=%d move=%s depth=%d score=%d nodes=%" PRIu64
               " cutoffs=%.1f%% tt_hits=%" PRIu64 " time=%dms\n",
               ai_difficulty_name(difficulties[i]), found ? 1 : 0, notation,
               stats.depth, stats.score, stats.nodes, cutoff_ratio * 100.0,
               stats.tt_hits, stats.elapsed_ms);
    }
}

static GameState midgame_position(void)
{
    GameState state;
    game_init(&state);
    uint64_t rng = BENCH_SEED ^ 0xABCDEFULL;
    for (int i = 0; i < 20 && state.result == GAME_RESULT_ONGOING; ++i) {
        if (game_must_pass(&state)) {
            (void)game_pass(&state);
            continue;
        }
        (void)game_apply_move(&state, random_move(&state, &rng));
    }
    return state;
}

static void build_one_move_endgame(GameState *state)
{
    static const char *const rows[REVERSI_BOARD_SIZE] = {
        "WB.WWWW.",
        "WWBBBWWB",
        "WBWWWWWB",
        "WBWWWBWB",
        "WWWWBBWB",
        "WBWWBWWB",
        "WWBBBWBB",
        "WBBBBBBB"
    };

    game_init(state);
    board_clear(&state->board);
    for (int row = 0; row < REVERSI_BOARD_SIZE; ++row) {
        for (int col = 0; col < REVERSI_BOARD_SIZE; ++col) {
            const char cell = rows[row][col];
            const Stone stone = cell == 'B' ? STONE_BLACK
                : (cell == 'W' ? STONE_WHITE : STONE_EMPTY);
            (void)board_set(&state->board, row, col, stone);
        }
    }
    state->current_player = STONE_WHITE;
    state->result = GAME_RESULT_ONGOING;
}

int main(int argc, char **argv)
{
    int games = 4;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--games") == 0 && i + 1 < argc) {
            games = atoi(argv[++i]);
            if (games < 1) {
                games = 1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--games N]\n", argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    printf("Reversi AI benchmark\n");
    printf("compiler=%s\n", __VERSION__);
    printf("seed=0x%" PRIX64 " games=%d\n\n", (uint64_t)BENCH_SEED, games);

    AiConfig easy = ai_default_config(AI_DIFFICULTY_EASY, 1);
    AiConfig medium = ai_default_config(AI_DIFFICULTY_MEDIUM, 1);
    AiConfig hard = ai_default_config(AI_DIFFICULTY_HARD, 1);

    run_matchup("Easy", BENCH_AI, &easy, "Random", BENCH_RANDOM, NULL, games);
    run_matchup("Medium", BENCH_AI, &medium,
                "Random", BENCH_RANDOM, NULL, games);
    run_matchup("Hard", BENCH_AI, &hard,
                "Random", BENCH_RANDOM, NULL, games);
    run_matchup("Easy", BENCH_AI, &easy, "Greedy", BENCH_GREEDY, NULL, games);
    run_matchup("Medium", BENCH_AI, &medium,
                "Greedy", BENCH_GREEDY, NULL, games);
    run_matchup("Hard", BENCH_AI, &hard,
                "Greedy", BENCH_GREEDY, NULL, games);
    run_matchup("Easy", BENCH_AI, &easy,
                "Medium", BENCH_AI, &medium, games);
    run_matchup("Medium", BENCH_AI, &medium,
                "Hard", BENCH_AI, &hard, games);
    run_matchup("Easy", BENCH_AI, &easy,
                "Hard", BENCH_AI, &hard, games);

    GameState opening;
    game_init(&opening);
    probe_position("opening", &opening);
    GameState midgame = midgame_position();
    probe_position("midgame", &midgame);
    GameState endgame;
    build_one_move_endgame(&endgame);
    probe_position("endgame (one move)", &endgame);

    return 0;
}
