#!/usr/bin/env bash
# Verify a packaged release without using the source tree.
# Usage: scripts/verify-package.sh path/to/Reversi-*.zip
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 path/to/Reversi-*.zip" >&2
    exit 2
fi

archive="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
tmp="$(mktemp -d)"
caffeinate_pid=""
cleanup() {
    if [ -n "${caffeinate_pid}" ]; then
        kill "${caffeinate_pid}" 2>/dev/null || true
    fi
    rm -rf "${tmp}"
}
trap cleanup EXIT

if command -v caffeinate >/dev/null 2>&1; then
    caffeinate -u -t 15 &
    caffeinate_pid=$!
    sleep 1
fi

unzip -q "${archive}" -d "${tmp}/package"
root="$(find "${tmp}/package" -maxdepth 1 -mindepth 1 -type d | head -n 1)"
if [ -z "${root}" ]; then
    echo "Package root not found" >&2
    exit 1
fi

if [ -x "${root}/Reversi.app/Contents/MacOS/Reversi" ]; then
    exe="${root}/Reversi.app/Contents/MacOS/Reversi"
    assets="${root}/Reversi.app/Contents/Resources/assets"
elif [ -x "${root}/reversi.app/Contents/MacOS/reversi" ]; then
    exe="${root}/reversi.app/Contents/MacOS/reversi"
    assets="${root}/reversi.app/Contents/Resources/assets"
elif [ -x "${root}/reversi" ]; then
    exe="${root}/reversi"
    assets="${root}/assets"
elif [ -f "${root}/reversi.exe" ]; then
    exe="${root}/reversi.exe"
    assets="${root}/assets"
else
    echo "Executable not found in package" >&2
    exit 1
fi

for asset in \
    "${assets}/fonts/Inter-Regular.ttf" \
    "${assets}/fonts/Inter-SemiBold.ttf" \
    "${assets}/fonts/OFL.txt"; do
    if [ ! -f "${asset}" ]; then
        echo "Missing runtime asset: ${asset}" >&2
        exit 1
    fi
done

export HOME="${tmp}/home"
export XDG_DATA_HOME="${tmp}/home"
export APPDATA="${tmp}/home"
mkdir -p "${HOME}"

"${exe}" --version
if ! (cd / && "${exe}" --frames 2 > "${tmp}/run.log" 2>&1); then
    echo "Packaged application failed to launch" >&2
    cat "${tmp}/run.log" >&2
    exit 1
fi
grep -q "loaded UI fonts" "${tmp}/run.log" || {
    echo "Bundled fonts were not loaded" >&2
    cat "${tmp}/run.log" >&2
    exit 1
}

echo "PASS: ${archive}"
