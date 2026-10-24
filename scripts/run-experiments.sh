#!/usr/bin/env bash
# Runs the committed experiment configs and refreshes the static report.
# Heavy by design; CI only runs the fast experiment.regression test.
set -euo pipefail
cd "$(dirname "$0")/.."

BIN=${BIN:-build-release/bin/reversi_experiment}
CORPUS=${CORPUS:-build-release/bin/reversi_ai_corpus}
ENDGAME=${ENDGAME:-build-release/reversi_endgame_tests}
STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

if [ ! -x "$BIN" ]; then
    echo "missing $BIN; configure and build the Release tree first" >&2
    exit 1
fi

mkdir -p docs/experiments/rules-throughput docs/experiments/endgame-verify
for config in experiments/*.exp; do
    name=$(basename "$config" .exp)
    "$BIN" "$config" "docs/experiments/$name"
done

{
    echo "Command: $CORPUS"
    echo "Date: $STAMP"
    echo
    "$CORPUS"
} > docs/experiments/rules-throughput/corpus.txt

{
    echo "Command: $ENDGAME"
    echo "Date: $STAMP"
    echo
    "$ENDGAME"
} > docs/experiments/endgame-verify/verify.txt

python3 scripts/make-experiment-index.py
