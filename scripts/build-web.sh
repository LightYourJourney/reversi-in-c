#!/usr/bin/env bash
# Builds the WebAssembly target and assembles a deployable static directory.
#
#   scripts/build-web.sh
#
# Environment:
#   EMSDK_DIR    Emscripten SDK root (default: ~/emsdk)
#   BUILD_TYPE   CMake build type (default: Release)
#   JOBS         Parallel build jobs (default: CPU count)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
emsdk_dir="${EMSDK_DIR:-$HOME/emsdk}"
build_dir="${root}/build-web"
dist_dir="${root}/web/dist"
build_type="${BUILD_TYPE:-Release}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [ ! -f "${emsdk_dir}/emsdk_env.sh" ]; then
    echo "Emscripten SDK not found at ${emsdk_dir}" >&2
    echo "Install a pinned version, e.g. ${emsdk_dir}/emsdk install 6.0.13" >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${emsdk_dir}/emsdk_env.sh" >/dev/null

expected_version="${EMSDK_EXPECTED_VERSION:-6.0.13}"
actual_version="$(emcc --version | head -n 1)"
if [[ "${actual_version}" != *" ${expected_version} "* ]]; then
    if [ "${REVERSI_WEB_ALLOW_ANY_EMSDK:-0}" != "1" ]; then
        echo "Expected Emscripten ${expected_version}, found:" >&2
        echo "  ${actual_version}" >&2
        echo "Set REVERSI_WEB_ALLOW_ANY_EMSDK=1 to override." >&2
        exit 1
    fi
fi

emcmake cmake -S "${root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DREVERSI_BUILD_TESTS=OFF \
    -DREVERSI_FORCE_FETCH_RAYLIB=ON
cmake --build "${build_dir}" --parallel "${jobs}"

rm -rf "${dist_dir}"
mkdir -p "${dist_dir}"

cp "${build_dir}/bin/reversi.js" \
   "${build_dir}/bin/reversi.wasm" \
   "${build_dir}/bin/reversi.data" \
   "${dist_dir}/"
cp "${build_dir}/bin/ai_worker.js" \
   "${build_dir}/bin/ai_worker.wasm" \
   "${dist_dir}/"
cp "${root}/web/app.js" \
   "${root}/web/style.css" \
   "${root}/web/ai_worker_boot.js" \
   "${root}/web/_headers" \
   "${root}/cmake/icons/reversi-1024.png" \
   "${dist_dir}/"
mv "${dist_dir}/reversi-1024.png" "${dist_dir}/icon.png"
cp "${root}/cmake/icons/reversi.ico" "${dist_dir}/favicon.ico"

version="$(cat "${root}/VERSION")"
sed "s|%REVERSI_VERSION%|${version}|g" \
    "${build_dir}/bin/reversi.html" > "${dist_dir}/index.html"

echo "Web build ready: ${dist_dir}"
ls -lh "${dist_dir}"
