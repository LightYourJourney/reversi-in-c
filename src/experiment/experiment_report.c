#include "experiment/experiment.h"

#include "core/board.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define experiment_mkdir(path) _mkdir(path)
#else
#define experiment_mkdir(path) mkdir((path), 0755)
#endif

static void report_error(char *error, size_t error_size, const char *text)
{
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", text);
    }
}

typedef struct TextBuffer {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} TextBuffer;

static void buffer_init(TextBuffer *buffer)
{
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->failed = false;
}

static void buffer_append(TextBuffer *buffer, const char *text)
{
    if (buffer->failed) {
        return;
    }
    const size_t add = strlen(text);
    if (buffer->length + add + 1 > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 1024 : buffer->capacity;
        while (capacity < buffer->length + add + 1) {
            capacity *= 2;
        }
        char *grown = realloc(buffer->data, capacity);
        if (grown == NULL) {
            buffer->failed = true;
            return;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, add + 1);
    buffer->length += add;
}

static void buffer_printf(TextBuffer *buffer, const char *format, ...)
{
    if (buffer->failed) {
        return;
    }
    va_list args;
    va_start(args, format);
    char small[256];
    const int needed = vsnprintf(small, sizeof(small), format, args);
    va_end(args);
    if (needed < 0) {
        buffer->failed = true;
        return;
    }
    if ((size_t)needed < sizeof(small)) {
        buffer_append(buffer, small);
        return;
    }
    char *large = malloc((size_t)needed + 1);
    if (large == NULL) {
        buffer->failed = true;
        return;
    }
    va_start(args, format);
    vsnprintf(large, (size_t)needed + 1, format, args);
    va_end(args);
    buffer_append(buffer, large);
    free(large);
}

static void buffer_free(TextBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void json_string(TextBuffer *buffer, const char *text)
{
    buffer_append(buffer, "\"");
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0';
         ++c) {
        switch (*c) {
        case '"':
            buffer_append(buffer, "\\\"");
            break;
        case '\\':
            buffer_append(buffer, "\\\\");
            break;
        case '\n':
            buffer_append(buffer, "\\n");
            break;
        case '\r':
            buffer_append(buffer, "\\r");
            break;
        case '\t':
            buffer_append(buffer, "\\t");
            break;
        case '<':
            buffer_append(buffer, "\\u003c");
            break;
        default:
            if (*c < 0x20) {
                buffer_printf(buffer, "\\u%04x", *c);
            } else {
                buffer_printf(buffer, "%c", *c);
            }
            break;
        }
    }
    buffer_append(buffer, "\"");
}

static const char *engine_kind_name(ExperimentEngineKind kind)
{
    switch (kind) {
    case EXPERIMENT_ENGINE_RANDOM:
        return "random";
    case EXPERIMENT_ENGINE_GREEDY:
        return "greedy";
    case EXPERIMENT_ENGINE_AI:
        return "ai";
    case EXPERIMENT_ENGINE_SEARCH:
    default:
        return "search";
    }
}

static const char *result_name(GameResult result)
{
    switch (result) {
    case GAME_RESULT_BLACK_WIN:
        return "B";
    case GAME_RESULT_WHITE_WIN:
        return "W";
    case GAME_RESULT_DRAW:
        return "D";
    default:
        return "-";
    }
}

static void json_engine(TextBuffer *buffer, const ExperimentEngine *engine)
{
    buffer_append(buffer, "{");
    buffer_append(buffer, "\"name\":");
    json_string(buffer, engine->name);
    buffer_printf(buffer, ",\"kind\":\"%s\"", engine_kind_name(engine->kind));
    buffer_printf(buffer, ",\"difficulty\":%d", (int)engine->difficulty);
    buffer_printf(buffer, ",\"depth\":%d", engine->max_depth);
    buffer_printf(buffer, ",\"time_ms\":%d", engine->time_budget_ms);
    buffer_printf(buffer, ",\"max_nodes\":%" PRIu64, engine->max_nodes);
    buffer_printf(buffer, ",\"tt\":%s", engine->use_tt ? "true" : "false");
    buffer_printf(buffer, ",\"endgame\":%s",
                  engine->use_endgame ? "true" : "false");
    buffer_printf(buffer, ",\"seed\":%u", engine->seed);
    buffer_append(buffer, "}");
}

static void json_stats(TextBuffer *buffer,
                       const ExperimentEngineStats *stats)
{
    buffer_printf(buffer,
                  "{\"moves\":%d,\"nodes\":%" PRIu64
                  ",\"nps\":%.1f,\"p50_ms\":%d,\"p95_ms\":%d"
                  ",\"max_ms\":%d,\"max_depth\":%d,\"cutoffs\":%" PRIu64
                  ",\"tt_hits\":%" PRIu64 ",\"timeouts\":%d"
                  ",\"node_limits\":%d,\"cancellations\":%d}",
                  stats->moves, stats->nodes, stats->nps, stats->p50_ms,
                  stats->p95_ms, stats->max_ms, stats->max_depth,
                  stats->cutoffs, stats->tt_hits, stats->timeouts,
                  stats->node_limits, stats->cancellations);
}

static void build_json(const ExperimentResult *result, TextBuffer *buffer)
{
    const ExperimentSummary *s = &result->summary;
    buffer_append(buffer, "{");
    buffer_append(buffer, "\"format\":\"reversi-experiment-1\",");
    buffer_append(buffer, "\"name\":");
    json_string(buffer, result->config.name);
    buffer_printf(buffer, ",\"seed\":%u", result->config.seed);
    buffer_printf(buffer, ",\"games\":%d,\"openings\":%d,\"opening_ply\":%d"
                  ",\"max_plies\":%d",
                  result->config.games, result->config.openings,
                  result->config.opening_plies, result->config.max_plies);
    buffer_append(buffer, ",\"manifest\":{");
    buffer_append(buffer, "\"commit\":");
    json_string(buffer, result->commit);
    buffer_append(buffer, ",\"os\":");
    json_string(buffer, result->os);
    buffer_append(buffer, ",\"cpu\":");
    json_string(buffer, result->cpu);
    buffer_append(buffer, ",\"compiler\":");
    json_string(buffer, result->compiler);
    buffer_append(buffer, ",\"build_type\":");
    json_string(buffer, result->build_type);
    buffer_append(buffer, ",\"data_version\":");
    json_string(buffer, result->data_version);
    buffer_append(buffer, "}");
    buffer_append(buffer, ",\"engines\":[");
    json_engine(buffer, &result->config.engines[0]);
    buffer_append(buffer, ",");
    json_engine(buffer, &result->config.engines[1]);
    buffer_append(buffer, "]");
    buffer_printf(buffer,
                  ",\"summary\":{\"games\":%d,\"completed\":%d,\"a_wins\":%d"
                  ",\"b_wins\":%d,\"draws\":%d,\"illegal\":%d"
                  ",\"timeouts\":%d,\"aborted\":%d"
                  ",\"a_black_wins\":%d,\"a_white_wins\":%d"
                  ",\"b_black_wins\":%d,\"b_white_wins\":%d"
                  ",\"a_score\":%.6f,\"score_low\":%.6f"
                  ",\"score_high\":%.6f,\"elo\":%.2f,\"elo_low\":%.2f"
                  ",\"elo_high\":%.2f,\"elo_defined\":%s"
                  ",\"peak_rss_kb\":%" PRIu64 "}",
                  s->games, s->completed, s->a_wins, s->b_wins, s->draws,
                  s->illegal, s->timeouts, s->aborted, s->a_black_wins,
                  s->a_white_wins, s->b_black_wins, s->b_white_wins,
                  s->a_score, s->score_low, s->score_high, s->elo,
                  s->elo_low, s->elo_high,
                  s->elo_defined ? "true" : "false", s->peak_rss_kb);
    buffer_append(buffer, ",\"a_stats\":");
    json_stats(buffer, &s->a_stats);
    buffer_append(buffer, ",\"b_stats\":");
    json_stats(buffer, &s->b_stats);
    buffer_append(buffer, "}");
}

static bool write_text_file(const char *dir, const char *name,
                            const char *data, size_t length,
                            char *error, size_t error_size)
{
    char path[1024];
    const int written = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        report_error(error, error_size, "output path is too long");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "cannot open %s", path);
        }
        return false;
    }
    const bool ok = length == 0 || fwrite(data, 1, length, file) == length;
    fclose(file);
    if (!ok) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "cannot write %s", path);
        }
    }
    return ok;
}

static void write_engine_config(TextBuffer *buffer, const char *slot,
                                const ExperimentEngine *engine)
{
    buffer_printf(buffer, "%s kind=%s difficulty=%s depth=%d time=%d "
                  "nodes=%" PRIu64 " tt=%s endgame=%s label=%s\n",
                  slot, engine_kind_name(engine->kind),
                  engine->difficulty == AI_DIFFICULTY_EASY ? "easy"
                      : engine->difficulty == AI_DIFFICULTY_MEDIUM
                          ? "medium" : "hard",
                  engine->max_depth, engine->time_budget_ms,
                  engine->max_nodes, engine->use_tt ? "on" : "off",
                  engine->use_endgame ? "on" : "off", engine->name);
}

static bool write_config_file(const ExperimentResult *result,
                              const char *dir, char *error,
                              size_t error_size)
{
    TextBuffer buffer;
    buffer_init(&buffer);
    buffer_append(&buffer, "version 1\n");
    buffer_printf(&buffer, "name %s\n", result->config.name);
    buffer_printf(&buffer, "seed %u\n", result->config.seed);
    buffer_printf(&buffer, "games %d\n", result->config.games);
    buffer_printf(&buffer, "openings %d\n", result->config.openings);
    buffer_printf(&buffer, "opening-ply %d\n", result->config.opening_plies);
    buffer_printf(&buffer, "max-plies %d\n", result->config.max_plies);
    write_engine_config(&buffer, "engine-a", &result->config.engines[0]);
    write_engine_config(&buffer, "engine-b", &result->config.engines[1]);
    const bool ok = !buffer.failed
        && write_text_file(dir, "config.exp", buffer.data, buffer.length,
                           error, error_size);
    buffer_free(&buffer);
    return ok;
}

static bool write_games_csv(const ExperimentResult *result, const char *dir,
                            char *error, size_t error_size)
{
    TextBuffer buffer;
    buffer_init(&buffer);
    buffer_append(&buffer,
        "game,opening,a_color,plies,passes,result,black_discs,white_discs,"
        "illegal,timed_out,aborted\n");
    for (int i = 0; i < result->game_count; ++i) {
        const ExperimentGame *game = &result->games[i];
        buffer_printf(&buffer, "%d,%d,%c,%d,%d,%s,%d,%d,%d,%d,%d\n",
                      game->index, game->opening,
                      game->a_color == STONE_BLACK ? 'B' : 'W',
                      game->plies, game->passes,
                      result_name(game->result), game->black_discs,
                      game->white_discs, game->illegal ? 1 : 0,
                      game->timed_out ? 1 : 0, game->aborted ? 1 : 0);
    }
    const bool ok = !buffer.failed
        && write_text_file(dir, "games.csv", buffer.data, buffer.length,
                           error, error_size);
    buffer_free(&buffer);
    return ok;
}

static bool write_moves_csv(const ExperimentResult *result, const char *dir,
                            char *error, size_t error_size)
{
    TextBuffer buffer;
    buffer_init(&buffer);
    buffer_append(&buffer,
        "game,ply,engine,color,move,depth,score,nodes,cutoffs,tt_hits,"
        "elapsed_ms,cancelled,timed_out,node_limit\n");
    for (int i = 0; i < result->move_count; ++i) {
        const ExperimentMoveSample *sample = &result->moves[i];
        char notation[3];
        if (!move_to_notation(sample->move, notation)) {
            snprintf(notation, sizeof(notation), "--");
        }
        buffer_printf(&buffer,
                      "%d,%d,%d,%c,%s,%d,%d,%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%d,%d,%d,%d\n",
                      sample->game, sample->ply, sample->engine,
                      sample->color == STONE_BLACK ? 'B' : 'W', notation,
                      sample->depth, sample->score, sample->nodes,
                      sample->cutoffs, sample->tt_hits, sample->elapsed_ms,
                      sample->cancelled ? 1 : 0,
                      sample->timed_out ? 1 : 0,
                      sample->node_limit ? 1 : 0);
    }
    const bool ok = !buffer.failed
        && write_text_file(dir, "moves.csv", buffer.data, buffer.length,
                           error, error_size);
    buffer_free(&buffer);
    return ok;
}

static void build_markdown(const ExperimentResult *result, TextBuffer *buffer)
{
    const ExperimentSummary *s = &result->summary;
    const ExperimentEngine *a = &result->config.engines[0];
    const ExperimentEngine *b = &result->config.engines[1];

    buffer_printf(buffer, "# Experiment: %s\n\n", result->config.name);
    buffer_printf(buffer, "- Commit: `%s`\n", result->commit);
    buffer_printf(buffer, "- OS: %s\n", result->os);
    buffer_printf(buffer, "- CPU: %s\n", result->cpu);
    buffer_printf(buffer, "- Compiler: %s\n", result->compiler);
    buffer_printf(buffer, "- Build: %s\n", result->build_type);
    buffer_printf(buffer, "- Seed: %u\n", result->config.seed);
    buffer_printf(buffer, "- Games: %d (completed %d)\n", s->games,
                  s->completed);
    buffer_printf(buffer, "- Openings: %d, opening ply %d, max plies %d\n\n",
                  result->config.openings, result->config.opening_plies,
                  result->config.max_plies);

    buffer_printf(buffer, "## Engines\n\n");
    buffer_printf(buffer, "| | A: %s | B: %s |\n|---|---|---|\n", a->name,
                  b->name);
    buffer_printf(buffer, "| kind | %s | %s |\n", engine_kind_name(a->kind),
                  engine_kind_name(b->kind));
    buffer_printf(buffer, "| difficulty | %d | %d |\n", (int)a->difficulty,
                  (int)b->difficulty);
    buffer_printf(buffer, "| depth | %d | %d |\n", a->max_depth,
                  b->max_depth);
    buffer_printf(buffer, "| time budget | %d ms | %d ms |\n",
                  a->time_budget_ms, b->time_budget_ms);
    buffer_printf(buffer, "| node cap | %" PRIu64 " | %" PRIu64 " |\n",
                  a->max_nodes, b->max_nodes);
    buffer_printf(buffer, "| TT | %s | %s |\n", a->use_tt ? "on" : "off",
                  b->use_tt ? "on" : "off");
    buffer_printf(buffer, "| endgame solver | %s | %s |\n\n",
                  a->use_endgame ? "on" : "off",
                  b->use_endgame ? "on" : "off");

    buffer_printf(buffer, "## Results\n\n");
    buffer_printf(buffer, "| Metric | Value |\n|---|---|\n");
    buffer_printf(buffer, "| A wins | %d |\n", s->a_wins);
    buffer_printf(buffer, "| B wins | %d |\n", s->b_wins);
    buffer_printf(buffer, "| Draws | %d |\n", s->draws);
    buffer_printf(buffer, "| A wins as Black | %d |\n", s->a_black_wins);
    buffer_printf(buffer, "| A wins as White | %d |\n", s->a_white_wins);
    buffer_printf(buffer, "| Illegal games | %d |\n", s->illegal);
    buffer_printf(buffer, "| Timeouts | %d |\n", s->timeouts);
    buffer_printf(buffer, "| Aborted | %d |\n", s->aborted);
    if (s->elo_defined) {
        buffer_printf(buffer,
                      "| A score (draw=0.5) | %.3f (95%% CI %.3f..%.3f) |\n",
                      s->a_score, s->score_low, s->score_high);
        buffer_printf(buffer, "| Elo difference | %+.0f (95%% CI %+.0f..%+.0f) |\n",
                      s->elo, s->elo_low, s->elo_high);
    } else {
        buffer_printf(buffer, "| A score | n/a (no completed games) |\n");
    }
    buffer_printf(buffer, "\nElo is a two-engine logistic estimate from the "
                  "A score; draws count as 0.5. The interval is a Wilson "
                  "interval transformed to Elo. Small samples remain "
                  "uncertain even when all games go one way.\n\n");

    buffer_printf(buffer, "## Search performance\n\n");
    buffer_printf(buffer, "| Metric | A | B |\n|---|---|---|\n");
    buffer_printf(buffer, "| Moves | %d | %d |\n", s->a_stats.moves,
                  s->b_stats.moves);
    buffer_printf(buffer, "| Nodes | %" PRIu64 " | %" PRIu64 " |\n",
                  s->a_stats.nodes, s->b_stats.nodes);
    buffer_printf(buffer, "| NPS | %.0f | %.0f |\n", s->a_stats.nps,
                  s->b_stats.nps);
    buffer_printf(buffer, "| P50 | %d ms | %d ms |\n", s->a_stats.p50_ms,
                  s->b_stats.p50_ms);
    buffer_printf(buffer, "| P95 | %d ms | %d ms |\n", s->a_stats.p95_ms,
                  s->b_stats.p95_ms);
    buffer_printf(buffer, "| Max depth | %d | %d |\n",
                  s->a_stats.max_depth, s->b_stats.max_depth);
    buffer_printf(buffer, "| Cut rate | %.1f%% | %.1f%% |\n",
                  s->a_stats.nodes > 0
                      ? 100.0 * (double)s->a_stats.cutoffs
                          / (double)s->a_stats.nodes : 0.0,
                  s->b_stats.nodes > 0
                      ? 100.0 * (double)s->b_stats.cutoffs
                          / (double)s->b_stats.nodes : 0.0);
    buffer_printf(buffer, "| TT hit rate | %.1f%% | %.1f%% |\n",
                  s->a_stats.nodes > 0
                      ? 100.0 * (double)s->a_stats.tt_hits
                          / (double)s->a_stats.nodes : 0.0,
                  s->b_stats.nodes > 0
                      ? 100.0 * (double)s->b_stats.tt_hits
                          / (double)s->b_stats.nodes : 0.0);
    buffer_printf(buffer, "| Timeouts | %d | %d |\n",
                  s->a_stats.timeouts, s->b_stats.timeouts);
    buffer_printf(buffer, "| Node cap hits | %d | %d |\n",
                  s->a_stats.node_limits, s->b_stats.node_limits);
    buffer_printf(buffer, "\nRaw data: `games.csv`, `moves.csv`, "
                  "`summary.json`. The HTML report renders the same JSON.\n");
}

static void build_html(const TextBuffer *json, TextBuffer *buffer)
{
    buffer_append(buffer,
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Reversi experiment</title><style>"
        "body{font-family:system-ui,sans-serif;margin:2rem;max-width:900px;"
        "background:#111;color:#eee}table{border-collapse:collapse;margin:1rem 0}"
        "td,th{border:1px solid #444;padding:.35rem .7rem;text-align:left}"
        "th{background:#222}.bar{height:1.1rem;background:#3b82f6;"
        "margin:.2rem 0}.bar.b{background:#f59e0b}.row{display:flex;"
        "align-items:center;gap:.6rem}.row span{min-width:5rem}"
        "small{color:#aaa}a{color:#7dd3fc}</style></head><body>");
    buffer_append(buffer, "<h1>Reversi experiment</h1>");
    buffer_append(buffer, "<div id=\"app\"></div>");
    buffer_append(buffer,
        "<p><a href=\"games.csv\">games.csv</a> · "
        "<a href=\"moves.csv\">moves.csv</a> · "
        "<a href=\"summary.json\">summary.json</a></p>");
    buffer_append(buffer,
        "<p><small>All values below are read from the embedded summary; "
        "raw data is in the CSV files.</small></p>");
    buffer_append(buffer, "<script id=\"exp-data\" type=\"application/json\">");
    buffer_append(buffer, json->data != NULL ? json->data : "{}");
    buffer_append(buffer, "</script>");
    buffer_append(buffer,
        "<script>const d=JSON.parse(document.getElementById('exp-data')"
        ".textContent);const app=document.getElementById('app');"
        "function el(t,c,x){const e=document.createElement(t);"
        "if(c)e.className=c;if(x!==undefined)e.textContent=x;return e}"
        "function table(rows){const t=el('table');for(const r of rows){"
        "const tr=el('tr');for(const c of r){const cell=r.head?el('th'):"
        "el('td');cell.textContent=c;tr.appendChild(cell)}t.appendChild(tr)}"
        "return t}"
        "const s=d.summary,a=d.engines[0],b=d.engines[1];"
        "app.appendChild(el('p',null,`${a.name} vs ${b.name} · seed "
        "${d.seed} · ${s.games} games (${s.completed} completed)`));"
        "app.appendChild(el('h2',null,'Configuration'));"
        "const info=[['Commit',d.manifest.commit],['OS',d.manifest.os],"
        "['CPU',d.manifest.cpu],['Compiler',d.manifest.compiler],"
        "['Build',d.manifest.build_type],['Data',d.manifest.data_version]];"
        "app.appendChild(table([...info.map(x=>[x[0],x[1]])]));"
        "app.appendChild(el('h2',null,'Results'));"
        "const max=Math.max(1,s.a_wins,s.b_wins,s.draws);"
        "for(const [label,value,cls] of [[a.name+' wins',s.a_wins,''],"
        "[b.name+' wins',s.b_wins,'b'],['Draws',s.draws,'b']]){"
        "const row=el('div','row');row.appendChild(el('span',null,label));"
        "const bar=el('div','bar '+cls);bar.style.width=(value/max*60)+'%';"
        "row.appendChild(bar);row.appendChild(el('span',null,value));"
        "app.appendChild(row)}"
        "app.appendChild(table([['Metric','Value'],['A score',"
        "s.a_score.toFixed(3)+' (95% CI '+s.score_low.toFixed(3)+'..'+"
        "s.score_high.toFixed(3)+')'],['Elo',s.elo_defined?"
        "(s.elo>=0?'+':'')+s.elo.toFixed(0)+' ['+s.elo_low.toFixed(0)+"
        "'..'+s.elo_high.toFixed(0)+']':'n/a'],['Illegal',s.illegal],"
        "['Timeouts',s.timeouts],['Aborted',s.aborted]]));"
        "app.appendChild(el('h2',null,'Search performance'));"
        "const perf=[['Metric',a.name,b.name],['Moves',s.a_stats.moves,"
        "s.b_stats.moves],['Nodes',s.a_stats.nodes,s.b_stats.nodes],"
        "['NPS',s.a_stats.nps.toFixed(0),s.b_stats.nps.toFixed(0)],"
        "['P50 ms',s.a_stats.p50_ms,s.b_stats.p50_ms],['P95 ms',"
        "s.a_stats.p95_ms,s.b_stats.p95_ms],['Max depth',"
        "s.a_stats.max_depth,s.b_stats.max_depth],['Cut rate %',"
        "(s.a_stats.nodes?100*s.a_stats.cutoffs/s.a_stats.nodes:0).toFixed(1),"
        "(s.b_stats.nodes?100*s.b_stats.cutoffs/s.b_stats.nodes:0).toFixed(1)],"
        "['TT hit %',(s.a_stats.nodes?100*s.a_stats.tt_hits/"
        "s.a_stats.nodes:0).toFixed(1),(s.b_stats.nodes?100*s.b_stats.tt_hits/"
        "s.b_stats.nodes:0).toFixed(1)],['Timeouts',"
        "s.a_stats.timeouts,s.b_stats.timeouts],['Node cap',"
        "s.a_stats.node_limits,s.b_stats.node_limits]];"
        "app.appendChild(table(perf));"
        "app.appendChild(el('p',null,'Elo is a two-engine logistic estimate; "
        "draws count as 0.5. Small samples are uncertain.'));"
        "</script></body></html>");
}

bool experiment_write_all(const ExperimentResult *result, const char *dir,
                          char *error, size_t error_size)
{
    if (result == NULL || dir == NULL || dir[0] == '\0') {
        report_error(error, error_size, "invalid output directory");
        return false;
    }
    (void)experiment_mkdir(dir);

    if (!write_config_file(result, dir, error, error_size)
        || !write_games_csv(result, dir, error, error_size)
        || !write_moves_csv(result, dir, error, error_size)) {
        return false;
    }

    TextBuffer json;
    buffer_init(&json);
    build_json(result, &json);
    if (json.failed
        || !write_text_file(dir, "summary.json", json.data, json.length,
                            error, error_size)) {
        buffer_free(&json);
        if (json.failed) {
            report_error(error, error_size, "out of memory");
        }
        return false;
    }
    buffer_free(&json);

    TextBuffer markdown;
    buffer_init(&markdown);
    build_markdown(result, &markdown);
    const bool md_ok = !markdown.failed
        && write_text_file(dir, "report.md", markdown.data, markdown.length,
                           error, error_size);
    buffer_free(&markdown);
    if (!md_ok) {
        report_error(error, error_size, "cannot write report.md");
        return false;
    }

    TextBuffer json_again;
    buffer_init(&json_again);
    build_json(result, &json_again);
    TextBuffer html;
    buffer_init(&html);
    build_html(&json_again, &html);
    buffer_free(&json_again);
    const bool html_ok = !html.failed
        && write_text_file(dir, "report.html", html.data, html.length,
                           error, error_size);
    buffer_free(&html);
    if (!html_ok) {
        report_error(error, error_size, "cannot write report.html");
    }
    return html_ok;
}
