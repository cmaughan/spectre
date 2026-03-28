#!/usr/bin/env bash
# Take a megacity screenshot, convert to PNG, copy to tmp/screenshot.png, and optionally push.
#
# Usage:
#   scripts/megacity_screenshot.sh [--show-ui] [--push] [--delay <ms>] [--size <WxH>]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DELAY=15000
SIZE="2560x1440"
NO_UI="--no-ui"
PUSH=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --show-ui)   NO_UI=""; shift ;;
        --push)      PUSH=true; shift ;;
        --delay)     DELAY="$2"; shift 2 ;;
        --size)      SIZE="$2"; shift 2 ;;
        *)           echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Determine the built app path
if [[ "$(uname)" == "Darwin" ]]; then
    APP="$REPO_ROOT/build/draxul.app/Contents/MacOS/draxul"
else
    APP="$REPO_ROOT/build/draxul"
fi

if [[ ! -x "$APP" ]]; then
    echo "App not found at $APP — build first." >&2
    exit 1
fi

BMP_TMP="$(mktemp /tmp/megacity_shot_XXXXXX.bmp)"
trap 'rm -f "$BMP_TMP"' EXIT

echo "Capturing megacity screenshot (delay=${DELAY}ms, size=${SIZE})..."
"$APP" --host megacity $NO_UI \
    --screenshot "$BMP_TMP" \
    --screenshot-delay "$DELAY" \
    --screenshot-size "$SIZE"

# Convert BMP to PNG
OUTPUT="$REPO_ROOT/tmp/screenshot.png"
mkdir -p "$(dirname "$OUTPUT")"

if command -v sips &>/dev/null; then
    sips -s format png "$BMP_TMP" --out "$OUTPUT" >/dev/null 2>&1
elif command -v convert &>/dev/null; then
    convert "$BMP_TMP" "$OUTPUT"
elif command -v ffmpeg &>/dev/null; then
    ffmpeg -y -i "$BMP_TMP" "$OUTPUT" 2>/dev/null
else
    echo "No image converter found (sips/convert/ffmpeg). Copying BMP instead." >&2
    cp "$BMP_TMP" "$REPO_ROOT/tmp/screenshot.bmp"
    OUTPUT="$REPO_ROOT/tmp/screenshot.bmp"
fi

echo "Screenshot saved to $OUTPUT"

if $PUSH; then
    cd "$REPO_ROOT"
    git add tmp/screenshot.png 2>/dev/null || git add tmp/screenshot.bmp
    git commit -m "Update megacity screenshot"
    git push
    echo "Pushed to remote."
fi
