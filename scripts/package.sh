#!/usr/bin/env bash
# Build, test and package a release on macOS or Linux.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${root}/build-release}"
version="$(tr -d '[:space:]' < "${root}/VERSION")"

echo "Packaging Reversi ${version}"

# A release archive is meant to be everything an operator needs, so build the
# Web client first when the toolchain is present. Without emsdk the archive
# simply has no web/ directory (verify-package.sh reports that).
emsdk_dir="${EMSDK_DIR:-$HOME/emsdk}"
if [ -f "${emsdk_dir}/emsdk_env.sh" ]; then
    "${root}/scripts/build-web.sh"
else
    echo "Emscripten not found at ${emsdk_dir}: packaging without web/" >&2
fi

cmake -S "${root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DREVERSI_ENABLE_GUI_TESTS=OFF \
    -DREVERSI_FORCE_FETCH_RAYLIB=ON
cmake --build "${build_dir}" --config Release --parallel
ctest --test-dir "${build_dir}" -C Release --output-on-failure

rm -rf "${build_dir}/package"
mkdir -p "${build_dir}/package"
(cd "${build_dir}" && cpack -C Release -B package)

shopt -s nullglob
archives=("${build_dir}"/package/*.zip "${build_dir}"/package/*.tar.gz)
if [ "${#archives[@]}" -eq 0 ]; then
    echo "No package archives were produced" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    (cd "${build_dir}/package" && sha256sum ./*.zip ./*.tar.gz > SHA256SUMS)
else
    (cd "${build_dir}/package" && shasum -a 256 ./*.zip ./*.tar.gz > SHA256SUMS)
fi

echo "Packages:"
ls -lh "${build_dir}/package"
echo "Checksums: ${build_dir}/package/SHA256SUMS"
