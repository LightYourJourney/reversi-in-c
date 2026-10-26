#!/usr/bin/env bash
# Local-only online stack: authoritative server + static Web client.
# No public binding, no cloud resources, no deployment.
set -euo pipefail
cd "$(dirname "$0")/.."

SERVER=${SERVER:-build-release/bin/reversi_server}
if [ ! -x "$SERVER" ]; then
    echo "missing $SERVER; build the Release tree first" >&2
    exit 1
fi

"$SERVER" --bind 127.0.0.1 --port 8080 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true' EXIT

echo "Server: ws://127.0.0.1:8080"
echo "Web:    http://127.0.0.1:8000  (Online play, server 127.0.0.1:8080)"
echo "Desktop: run the Reversi app, choose Online play, same server address."
./scripts/serve-web.sh 8000
