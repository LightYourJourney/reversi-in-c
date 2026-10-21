#!/usr/bin/env bash
# Regenerates the application icon set from ImageMagick primitives.
# Outputs are committed; run this only when the design changes.
#
#   scripts/make-icon.sh
#
# Requires: ImageMagick 7 (`magick`) and macOS `iconutil` for the .icns.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${root}/cmake/icons"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

mkdir -p "${out}"

# Board colours mirror the default (dark) in-game palette.
board='#1E4938'
edge='#0F281F'
white='#F7F4EC'
white_edge='#CFC9BA'
black='#141814'
black_edge='#2C332C'

# Rounded board.
magick -size 1024x1024 xc:none \
    -fill "${board}" -stroke "${edge}" -strokewidth 24 \
    -draw 'roundrectangle 56,56 968,968 200,200' \
    "${tmp}/board.png"

# Four discs in the Othello starting arrangement (diagonal pairs).
magick -size 1024x1024 xc:none \
    -fill "${white}" -stroke "${white_edge}" -strokewidth 8 \
    -draw 'circle 384,384 384,266' \
    -draw 'circle 640,640 640,522' \
    "${tmp}/white.png"
magick -size 1024x1024 xc:none \
    -fill "${black}" -stroke "${black_edge}" -strokewidth 8 \
    -draw 'circle 640,384 640,266' \
    -draw 'circle 384,640 384,522' \
    "${tmp}/black.png"

magick "${tmp}/black.png" "${tmp}/white.png" -composite "${tmp}/discs.png"
magick "${tmp}/discs.png" \
    \( +clone -background '#000000' -shadow 45x14+0+12 \) \
    +swap -background none -layers merge +repage "${tmp}/discs-shadow.png"
magick "${tmp}/board.png" "${tmp}/discs-shadow.png" -composite \
    "${out}/reversi-1024.png"

# macOS icon set.
mkdir -p "${tmp}/Reversi.iconset"
for spec in \
    '16 icon_16x16.png' \
    '32 icon_16x16@2x.png' \
    '32 icon_32x32.png' \
    '64 icon_32x32@2x.png' \
    '128 icon_128x128.png' \
    '256 icon_128x128@2x.png' \
    '256 icon_256x256.png' \
    '512 icon_256x256@2x.png' \
    '512 icon_512x512.png' \
    '1024 icon_512x512@2x.png'
do
    size="${spec%% *}"
    name="${spec#* }"
    magick "${out}/reversi-1024.png" -resize "${size}x${size}" \
        "${tmp}/Reversi.iconset/${name}"
done
iconutil -c icns "${tmp}/Reversi.iconset" -o "${out}/Reversi.icns"

# Windows icon (multi-resolution).
magick "${out}/reversi-1024.png" \
    -define icon:auto-resize=256,128,64,48,32,16 \
    "${out}/reversi.ico"

echo "Wrote ${out}/reversi-1024.png, Reversi.icns, reversi.ico"
