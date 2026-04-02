#!/usr/bin/env sh
set -eu

MODE="debug"
FORCE_RECONFIGURE=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    debug|release)
      MODE="$1"
      shift
      ;;
    --reconfigure)
      FORCE_RECONFIGURE=1
      shift
      ;;
    --)
      shift
      break
      ;;
    *)
      break
      ;;
  esac
done

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT_DIR"

run() {
  echo
  echo "> $*"
  "$@"
}

check_metal_toolchain() {
  if [ "$(uname -s)" != "Darwin" ]; then
    return 0
  fi

  if ! xcrun --find metal >/dev/null 2>&1; then
    echo "Missing Metal compiler. Install Xcode Command Line Tools and the Metal toolchain." >&2
    echo "Suggested fix: xcodebuild -downloadComponent MetalToolchain" >&2
    exit 1
  fi

  if ! xcrun -sdk macosx metal -v >/dev/null 2>&1; then
    echo "The Metal compiler is present but not runnable because the Metal Toolchain is missing." >&2
    echo "Suggested fix: xcodebuild -downloadComponent MetalToolchain" >&2
    exit 1
  fi
}

cache_build_type() {
  if [ ! -f build/CMakeCache.txt ]; then
    return 1
  fi
  sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' build/CMakeCache.txt | head -n 1
}

should_configure() {
  config="$1"

  if [ "$FORCE_RECONFIGURE" -eq 1 ]; then
    return 0
  fi

  if [ ! -f build/CMakeCache.txt ]; then
    return 0
  fi

  cached_type=$(cache_build_type || true)
  if [ -z "$cached_type" ]; then
    return 0
  fi

  [ "$cached_type" != "$config" ]
}

case "$MODE" in
  debug)
    CONFIG="Debug"
    PRESET="mac-debug"
    ;;
  release)
    CONFIG="Release"
    PRESET="mac-release"
    ;;
  *)
    echo "Usage: $(basename "$0") [debug|release] [--reconfigure] [-- app args...]" >&2
    exit 2
    ;;
esac

echo
echo "=== $CONFIG ==="
check_metal_toolchain
if should_configure "$CONFIG"; then
  run cmake --preset "$PRESET"
else
  echo
  echo "> using existing CMake cache: build/CMakeCache.txt"
fi
run cmake --build build --parallel

BUNDLE_EXE="./build/draxul.app/Contents/MacOS/draxul"
PLAIN_EXE="./build/draxul"

if [ -x "$BUNDLE_EXE" ]; then
  EXE="$BUNDLE_EXE"
elif [ -x "$PLAIN_EXE" ]; then
  EXE="$PLAIN_EXE"
else
  echo "Missing executable: $BUNDLE_EXE" >&2
  exit 1
fi

echo
echo "> $EXE $*"
exec "$EXE" "$@"
