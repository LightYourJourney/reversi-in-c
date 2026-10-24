#!/usr/bin/env python3
"""Builds docs/experiments/index.html from the actual summary.json files."""
import html
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "experiments"


def esc(value):
    return html.escape(str(value), quote=True)


rows = []
for directory in sorted(ROOT.iterdir()):
    summary = directory / "summary.json"
    if not summary.is_file():
        continue
    data = json.loads(summary.read_text(encoding="utf-8"))
    s = data["summary"]
    engines = data["engines"]
    names = f"{esc(engines[0]['name'])} vs {esc(engines[1]['name'])}"
    score = (
        f"{s['a_score']:.3f} ({s['score_low']:.3f}..{s['score_high']:.3f})"
        if s["completed"]
        else "n/a"
    )
    elo = (
        f"{s['elo']:+.0f} ({s['elo_low']:+.0f}..{s['elo_high']:+.0f})"
        if s["elo_defined"]
        else "n/a"
    )
    a = data["a_stats"]
    b = data["b_stats"]
    rows.append(
        "<tr>"
        f"<td><a href=\"{esc(directory.name)}/report.html\">{esc(data['name'])}</a></td>"
        f"<td>{names}</td>"
        f"<td>{s['games']}</td>"
        f"<td>{s['completed']}</td>"
        f"<td>{s['a_wins']}/{s['draws']}/{s['b_wins']}</td>"
        f"<td>{score}</td>"
        f"<td>{elo}</td>"
        f"<td>{a['p50_ms']}/{b['p50_ms']}</td>"
        f"<td>{a['nodes']}/{b['nodes']}</td>"
        f"<td>{s['illegal']}/{s['timeouts']}/{s.get('node_limited', 0)}/{s['aborted']}</td>"
        f"<td><a href=\"{esc(directory.name)}/games.csv\">games</a> "
        f"<a href=\"{esc(directory.name)}/moves.csv\">moves</a> "
        f"<a href=\"{esc(directory.name)}/summary.json\">json</a></td>"
        "</tr>"
    )

page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Reversi experiments</title>
<style>
body {{ font-family: system-ui, sans-serif; margin: 2rem; background: #111; color: #eee; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #444; padding: .35rem .7rem; text-align: left; }}
th {{ background: #222; }}
a {{ color: #7dd3fc; }}
small {{ color: #aaa; }}
</style>
</head>
<body>
<h1>Reversi experiments</h1>
<p><small>Generated from each experiment's <code>summary.json</code>.
W/D/L is engine A's record. CI is a Wilson 95% interval over paired
openings (the independent unit); Elo is a two-engine logistic estimate with
draws counted as 0.5. Raw data links are per experiment.</small></p>
<table>
<tr><th>Experiment</th><th>Engines</th><th>Games</th><th>Completed</th>
<th>A W/D/L</th><th>A score (95% CI)</th><th>Elo (95% CI)</th><th>P50 ms A/B</th>
<th>Nodes A/B</th><th>Illegal/timeout/node-cap/aborted</th><th>Raw</th></tr>
{''.join(rows)}
</table>
<p><small>Non-match evidence: <a href="rules-throughput/corpus.txt">rules
throughput</a>, <a href="endgame-verify/verify.txt">exact endgame
verification</a>.</small></p>
</body>
</html>
"""
(ROOT / "index.html").write_text(page, encoding="utf-8")
print("wrote", ROOT / "index.html")
