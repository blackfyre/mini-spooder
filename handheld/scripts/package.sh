#!/bin/sh
# Zips MiniSpooder.pak (openspec change handheld-pak, design D12) from the
# device build: launch.sh, the arm64 binary and lib/libSDL2_gfx-1.0.so.0, to
# build/handheld/MiniSpooder.pak.zip. The binary is checked again first
# (check-glibc.sh), so a binary for the wrong machine or glibc is never
# zipped. Run from the repository root after handheld-build-device
# (mise run handheld-package).
set -eu

root=$(pwd)
arm64=$root/build/handheld/arm64
stage=$root/build/handheld/pak
zip=$root/build/handheld/MiniSpooder.pak.zip

for f in "$arm64/minispooder" "$arm64/lib/libSDL2_gfx-1.0.so.0"; do
  [ -f "$f" ] || { echo "package: $f is missing; run mise run handheld-build-device" >&2; exit 1; }
done
rm -rf "$stage" "$zip"
"$root/handheld/scripts/check-glibc.sh" "$arm64/minispooder"

mkdir -p "$stage/MiniSpooder.pak/lib"
cp "$root/handheld/pak/launch.sh" "$stage/MiniSpooder.pak/launch.sh"
cp "$arm64/minispooder" "$stage/MiniSpooder.pak/minispooder"
cp "$arm64/lib/libSDL2_gfx-1.0.so.0" "$stage/MiniSpooder.pak/lib/"
chmod 755 "$stage/MiniSpooder.pak/launch.sh" "$stage/MiniSpooder.pak/minispooder"
chmod 644 "$stage/MiniSpooder.pak/lib/libSDL2_gfx-1.0.so.0"
# Files only: no directory entries, so the zip lists exactly the three files.
(cd "$stage" && zip -q -X -D "$zip" MiniSpooder.pak/launch.sh MiniSpooder.pak/minispooder MiniSpooder.pak/lib/libSDL2_gfx-1.0.so.0)
unzip -l "$zip"
