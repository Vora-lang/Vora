#!/usr/bin/env bash
#
# build.sh — One-shot build + run for Vora (Linux / macOS)
#
# Usage:
#   ./build.sh                                    # build + run (x64, debug)
#   ./build.sh -a arm64 -c release                # cross-compile ARM64 release
#   ./build.sh -a x64 -c release -p               # build + generate .deb/.rpm
#   ./build.sh -a x86 -c debug examples/main.va   # run a specific script
#
# Equivalent to build.ps1 on Windows.

set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────
ARCH="x64"
CONFIG="debug"
PACKAGE=0

# ── Parse arguments ─────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [-a ARCH] [-c CONFIG] [-p] [--] [extra args for Vora]"
    echo ""
    echo "  -a ARCH    Target architecture: x64 | x86 | aarch64 | armhf (default: x64)"
    echo "  -c CONFIG  Build configuration: debug | release (default: debug)"
    echo "  -p         Generate package after build (Release only)"
    echo ""
    echo "  Linux presets: linux-x64, linux-x86, linux-aarch64, linux-armhf"
    echo "  macOS preset:  macos-universal"
    echo ""
    echo "Examples:"
    echo "  $0                                          # native x64 debug build + run"
    echo "  $0 -a arm64 -c release -p                   # cross-compile ARM64 + package"
    echo "  $0 -c release -p                            # native release + .deb/.rpm"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)
            ARCH="$2"; shift 2 ;;
        -c|--config)
            CONFIG="$2"; shift 2 ;;
        -p|--package)
            PACKAGE=1; shift ;;
        -h|--help)
            usage ;;
        --)
            shift; break ;;
        -*)
            echo "Unknown option: $1" >&2; usage ;;
        *)
            break ;;
    esac
done

# Validate arch
case "$ARCH" in
    x64|x86|aarch64|armhf) ;;
    *)
        echo "Error: unsupported architecture '$ARCH'" >&2
        echo "  Valid: x64, x86, aarch64, armhf" >&2
        exit 1 ;;
esac

# Validate config
case "$CONFIG" in
    debug|release) ;;
    *)
        echo "Error: unsupported config '$CONFIG'" >&2
        echo "  Valid: debug, release" >&2
        exit 1 ;;
esac

# Detect OS for preset prefix
OS="linux"
if [[ "$(uname -s)" == "Darwin" ]]; then
    OS="macos"
    # macOS only supports universal currently
    if [[ "$ARCH" != "x64" && "$ARCH" != "aarch64" ]]; then
        echo "Note: macOS uses 'macos-universal' preset (fat binary: x86_64 + arm64)" >&2
    fi
    ARCH="universal"
fi

PRESET="${OS}-${ARCH}-${CONFIG}"
BUILD_DIR="build/${PRESET}"

echo ""
echo "==== Vora Build System ===="
echo "  OS          : ${OS}"
echo "  Architecture: ${ARCH}"
echo "  Configuration: ${CONFIG}"
if [[ $PACKAGE -eq 1 ]]; then
    echo "  Package     : yes"
fi
echo "  Preset      : ${PRESET}"
echo ""

# ── Clean ───────────────────────────────────────────────────────────────────
echo "[1/5] Cleaning old build..."

if [[ -d "$BUILD_DIR" ]]; then
    rm -rf "$BUILD_DIR"
fi

# ── Configure ───────────────────────────────────────────────────────────────
echo "[2/5] Configuring CMake (preset: ${PRESET})..."

cmake --preset "$PRESET"

# ── Build ───────────────────────────────────────────────────────────────────
echo "[3/5] Building project..."

cmake --build --preset "$PRESET"

# ── Package ─────────────────────────────────────────────────────────────────
if [[ $PACKAGE -eq 1 ]]; then
    if [[ "$CONFIG" == "release" ]]; then
        echo "[4/5] Generating package..."
        cmake --build "$BUILD_DIR" --target package
        echo ""
        echo "Packages:"
        find "$BUILD_DIR" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' -o -name '*.tar.xz' -o -name '*.tar.gz' \) -exec basename {} \; 2>/dev/null || true
    else
        echo "[4/5] Package skipped — only available for release builds"
    fi
else
    echo "[4/5] Package skipped — use -p to generate installer"
fi

# ── Run ─────────────────────────────────────────────────────────────────────
echo "[5/5] Running Vora..."
echo ""

# Find executable (single-config: build/<preset>/Vora; multi-config: build/<preset>/<Config>/Vora)
EXE_PATH=""
for candidate in \
    "$BUILD_DIR/Vora" \
    "$BUILD_DIR/debug/Vora" \
    "$BUILD_DIR/release/Vora" \
    "$BUILD_DIR/Debug/Vora" \
    "$BUILD_DIR/Release/Vora"; do
    if [[ -f "$candidate" ]]; then
        EXE_PATH="$candidate"
        break
    fi
done

if [[ -z "$EXE_PATH" ]]; then
    echo ""
    echo "Executable not found. Searched:"
    echo "  $BUILD_DIR/Vora"
    echo "  $BUILD_DIR/{debug,release,Debug,Release}/Vora"
    exit 1
fi

"$EXE_PATH" "$@"

echo ""
echo "==== Build Success ===="

# Show available presets hint
echo ""
echo "Available presets: cmake --list-presets"
echo "Usage  : $0 -a arm64 -c release -p"
