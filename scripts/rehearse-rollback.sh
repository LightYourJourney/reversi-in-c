#!/usr/bin/env bash
# Rehearse the release-directory switch and its rollback, offline.
#
#   scripts/rehearse-rollback.sh --package build-release/package/Reversi-*.tar.gz
#
# Unpacks the release twice into a throwaway "staging" tree, breaks the newer
# one on purpose, then plays the operator's steps from
# docs/PRODUCTION_DEPLOYMENT.md §8 against a real process:
#
#   current -> releases/next (broken)  start -> health check must fail
#   current -> releases/prev (good)    start -> health check must pass
#
# Nothing here touches a live deployment: it uses its own port, its own
# directory and its own processes, and it never sends SIGTERM to anything it
# did not start. systemd itself is still not exercised on macOS.
set -euo pipefail

archive=""
port=""
while [ "$#" -gt 0 ]; do
    case "$1" in
    --package) archive="${2:-}"; shift 2 ;;
    --port) port="${2:-}"; shift 2 ;;
    *) echo "usage: $0 --package <archive> [--port N]" >&2; exit 2 ;;
    esac
done
if [ -z "${archive}" ] || [ ! -f "${archive}" ]; then
    echo "usage: $0 --package <archive> [--port N]" >&2
    exit 2
fi
archive="$(cd "$(dirname "${archive}")" && pwd)/$(basename "${archive}")"

root="$(cd "$(dirname "$0")/.." && pwd)"
here="${root}/scripts"
work="$(mktemp -d)"
pids=()
cleanup() {
    for pid in "${pids[@]:-}"; do
        [ -n "${pid}" ] && kill "${pid}" 2>/dev/null || true
    done
    rm -rf "${work}"
}
trap cleanup EXIT

log() { printf '%s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

mkdir -p "${work}/releases"
for name in prev next; do
    mkdir -p "${work}/releases/${name}"
    tar -xzf "${archive}" -C "${work}/releases/${name}" --strip-components=1
done

server_prev="${work}/releases/prev/bin/reversi_server"
[ -x "${server_prev}" ] || fail "package has no bin/reversi_server"

# Break the newer release the way a bad build would: it starts and exits.
broken="${work}/releases/next/bin/reversi_server"
printf '#!/bin/sh\necho "simulated broken release" >&2\nexit 1\n' > "${broken}"
chmod +x "${broken}"

if [ -z "${port}" ]; then
    port="$(/usr/bin/python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
fi
url="ws://127.0.0.1:${port}/"

start_current() {
    ln -sfn "${work}/releases/$1" "${work}/current"
    "${work}/current/bin/reversi_server" --bind 127.0.0.1 --port "${port}" \
        > "${work}/$1.log" 2>&1 &
    pids+=("$!")
    sleep 1
}

health() {
    node "${here}/check-server-health.mjs" --url "${url}" --timeout 2000 \
        > "${work}/health.json" 2>&1
}

log "staging tree: ${work}"
log "archive:      $(basename "${archive}")"
log "url:          ${url}"

# 1) New release is broken: the service must not pass its health check.
start_current next
if health; then
    fail "broken release passed the health check"
fi
log "PASS broken release rejected by health check"

# 2) Roll back to the previous release: service must come back.
for pid in "${pids[@]:-}"; do
    [ -n "${pid}" ] && kill "${pid}" 2>/dev/null || true
done
pids=()
sleep 0.3
start_current prev
health || fail "rollback did not restore a healthy server"
log "PASS rollback restored a healthy server"

# 3) Health check must stay non-destructive: it never creates a room.
if grep -q "created" "${work}/prev.log" 2>/dev/null; then
    fail "health check created a room"
fi
log "PASS health check created no room"

log "rollback rehearsal complete"
