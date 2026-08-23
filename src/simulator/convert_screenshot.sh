#!/usr/bin/env bash
#
# Convert the most recent simulator screenshot (1-bit BMP) to PNG.
#
# The simulator's SCREENSHOT command saves BMPs to fs_/screenshots/.
# BMPs are not viewable by most image tools or AI agents, so this script
# converts the newest screenshot and prints the resulting PNG path.
# Output goes to fs_/tmp/ — the simulator's own data dir — to avoid any
# /tmp sandboxing or permission issues.
#
# Usage (no arguments):
#   ./src/simulator/convert_screenshot.sh
#
# Prints the absolute path of the converted PNG on success.
#
set -euo pipefail

# Repo root = two levels up from this script (src/simulator/)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SHOTS="$ROOT/fs_/screenshots"
OUT_DIR="$ROOT/fs_/tmp"

mkdir -p "$OUT_DIR"

# Newest .bmp by modification time
BMP="$(ls -t "$SHOTS"/*.bmp 2>/dev/null | head -1 || true)"
if [ -z "$BMP" ]; then
    echo "ERROR: no screenshots found in $SHOTS" >&2
    echo "       send 'SCREENSHOT' to the simulator first" >&2
    exit 1
fi

PNG="$OUT_DIR/$(basename "${BMP%.bmp}").png"

if command -v sips >/dev/null 2>&1; then
    # macOS built-in converter
    sips -s format png "$BMP" --out "$PNG" >/dev/null
elif command -v magick >/dev/null 2>&1; then
    # ImageMagick 7
    magick "$BMP" "$PNG"
elif command -v convert >/dev/null 2>&1; then
    # ImageMagick 6
    convert "$BMP" "$PNG"
else
    echo "ERROR: no converter found (need macOS sips or ImageMagick)" >&2
    exit 1
fi

echo "$PNG"
