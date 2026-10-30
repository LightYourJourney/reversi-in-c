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

checksum="${archive}.sha256"
if [ ! -f "${checksum}" ]; then
    echo "Checksum file not found: ${checksum}" >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    (cd "$(dirname "${checksum}")" && sha256sum -c "$(basename "${checksum}")")
else
    (cd "$(dirname "${checksum}")" && shasum -a 256 -c "$(basename "${checksum}")")
fi

unzip -q "${archive}" -d "${tmp}/package"
shopt -s nullglob
roots=("${tmp}/package"/*/)
if [ "${#roots[@]}" -ne 1 ]; then
    echo "Expected exactly one package root, found ${#roots[@]}" >&2
    exit 1
fi
root="${roots[0]%/}"

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

if [ -d "${root}/Reversi.app" ]; then
    for document in LICENSE THIRD_PARTY_NOTICES.md; do
        if [ ! -f "${root}/Reversi.app/Contents/Resources/${document}" ]; then
            echo "Missing license in app bundle: ${document}" >&2
            exit 1
        fi
    done
else
    for document in LICENSE THIRD_PARTY_NOTICES.md; do
        if [ ! -f "${root}/${document}" ]; then
            echo "Missing license in package: ${document}" >&2
            exit 1
        fi
    done
fi

# The online server and the Web client ship in the same archive; an operator
# installs one directory. Both are checked from the extracted tree, never
# from the build directory.
server=""
for candidate in "${root}/bin/reversi_server" "${root}/bin/reversi_server.exe"; do
    if [ -x "${candidate}" ]; then
        server="${candidate}"
        break
    fi
done
if [ -z "${server}" ]; then
    echo "Missing server executable: bin/reversi_server" >&2
    exit 1
fi
if [ ! -f "${root}/web/index.html" ]; then
    echo "Missing Web client: web/index.html" >&2
    exit 1
fi
if ! "${server}" --help 2>&1 | grep -q "usage:"; then
    echo "Packaged server does not start" >&2
    exit 1
fi

if [ "$(uname -s)" = "Linux" ]; then
    missing="$(ldd "${exe}" | grep "not found" || true)"
    if [ -n "${missing}" ]; then
        echo "Missing shared libraries:" >&2
        echo "${missing}" >&2
        exit 1
    fi
fi

export HOME="${tmp}/home"
export XDG_DATA_HOME="${tmp}/home"
export APPDATA="${tmp}/home"
mkdir -p "${HOME}"

"${exe}" --version
for run in 1 2; do
    if ! (cd / && "${exe}" --frames 2 > "${tmp}/run-${run}.log" 2>&1); then
        echo "Packaged application failed on launch ${run}" >&2
        cat "${tmp}/run-${run}.log" >&2
        exit 1
    fi
done

grep -q "loaded UI fonts" "${tmp}/run-2.log" || {
    echo "Bundled fonts were not loaded" >&2
    cat "${tmp}/run-2.log" >&2
    exit 1
}

for name in settings.dat stats.dat; do
    if [ "$(find "${HOME}" -type f -name "${name}" | wc -l)" -eq 0 ]; then
        echo "Application did not persist ${name}" >&2
        exit 1
    fi
done

echo "PASS: ${archive}"
