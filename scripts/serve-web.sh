#!/usr/bin/env bash
# Serves the assembled Web build for local testing.
#
#   scripts/serve-web.sh [port]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
dist="${root}/web/dist"
port="${1:-8080}"

if [ ! -f "${dist}/index.html" ]; then
    echo "No Web build at ${dist}; run scripts/build-web.sh first." >&2
    exit 1
fi

echo "Serving ${dist} at http://127.0.0.1:${port}/"
exec python3 -m http.server "${port}" --bind 127.0.0.1 --directory "${dist}"
