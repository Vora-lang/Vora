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
ARCH=""
CONFIG=""
PACKAGE=0
CLEAN=0

# ── Parse arguments ─────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [-a ARCH] [-c CONFIG] [-p] [-C] [--] [extra args for Vora]"
    echo ""
    echo "  -a ARCH    Target architecture: x64 | x86 | aarch64 | armhf (default: x64)"
    echo "  -c CONFIG  Build configuration: debug | release (default: debug)"
    echo "  -p         Generate package after build (Release only)"
    echo "  -C         Force clean build (delete build directory before configuring)"
    echo ""
    echo "  Linux presets: linux-x64, linux-x86, linux-aarch64, linux-armhf"
    echo "  macOS preset:  macos-universal"
    echo ""
    echo "Examples:"
    echo "  $0                                          # native x64 debug build + run"
    echo "  $0 -C                                       # clean build + run"
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
        -C|--clean)
            CLEAN=1; shift ;;
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

ARCH="${ARCH:-x64}"
CONFIG="${CONFIG:-debug}"
# Validate arch

# ==== Interactive mode (no arguments) ====
if [[ -z "${ARCH:-}" && -z "${CONFIG:-}" && $PACKAGE -eq 0 && $CLEAN -eq 0 ]]; then
    echo ""
    echo "==== Vora Build (Interactive) ===="
    echo ""
    echo "Target architecture:"
    echo "  [1] x64       (default)"
    echo "  [2] x86       (32-bit)"
    echo "  [3] aarch64   (ARM 64-bit)"
    echo "  [4] armhf     (ARM 32-bit)"
    read -rp "Choice [1-4] (default 1): " arch_choice
    case "${arch_choice:-1}" in
        2) ARCH="x86" ;;
        3) ARCH="aarch64" ;;
        4) ARCH="armhf" ;;
        *) ARCH="x64" ;;
    esac
    echo ""
    echo "Build configuration:"
    echo "  [1] Debug   (default)"
    echo "  [2] Release"
    read -rp "Choice [1-2] (default 1): " config_choice
    case "${config_choice:-1}" in
        2) CONFIG="release" ;;
        *) CONFIG="debug" ;;
    esac
    if [[ "$CONFIG" == "release" ]]; then
        echo ""
        echo "Generate installer package?"
        echo "  [1] No    (default)"
        echo "  [2] Yes   (.deb / .rpm / .pkg.tar.xz)"
        read -rp "Choice [1-2] (default 1): " pkg_choice
        if [[ "${pkg_choice:-1}" == "2" ]]; then PACKAGE=1; fi
    fi
    echo ""
fi
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
if [[ $CLEAN -eq 1 ]]; then
    echo "[1/5] Cleaning old build..."
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
    fi
else
    echo "[1/5] Using existing build directory (use -C for clean build)..."
fi

# ── Configure ───────────────────────────────────────────────────────────────
echo "[2/5] Configuring CMake (preset: ${PRESET})..."

cmake --preset "$PRESET"

# ── Build ───────────────────────────────────────────────────────────────────
echo "[3/5] Building project..."

cmake --build --preset "$PRESET"

# ── Build LSP + DAP from Vora-LSP repo (Release + Package only) ─────────────
LSP_REPO="$(dirname "$0")/../Vora-LSP"
if [[ $PACKAGE -eq 1 && "$CONFIG" == "release" ]]; then
    echo "[4/6] Building vora-lsp + vora-dap from Vora-LSP..."
    if [[ -f "$LSP_REPO/CMakeLists.txt" ]]; then
        (cd "$LSP_REPO" && cmake -B build -DVORA_BUILD="$BUILD_DIR" > /dev/null 2>&1 && cmake --build build --config Release --target vora-lsp vora-dap) && {
            mkdir -p "$BUILD_DIR/Release"
            cp -f "$LSP_REPO/build/Release/vora-lsp" "$BUILD_DIR/Release/" 2>/dev/null || true
            cp -f "$LSP_REPO/build/Release/vora-dap" "$BUILD_DIR/Release/" 2>/dev/null || true
            cp -f "$LSP_REPO/vora-lsp" "$BUILD_DIR/Release/" 2>/dev/null || true
            cp -f "$LSP_REPO/vora-dap" "$BUILD_DIR/Release/" 2>/dev/null || true
            echo "  vora-lsp + vora-dap rebuilt and staged"
        } || echo "  Warning: Vora-LSP build failed, using existing binaries if present"
    else
        echo "  Vora-LSP repo not found at $LSP_REPO, using existing binaries"
    fi
else
    echo "[4/6] LSP/DAP rebuild skipped (requires -p -c release)"
fi

# ── Package ─────────────────────────────────────────────────────────────────
if [[ $PACKAGE -eq 1 ]]; then
    if [[ "$CONFIG" == "release" ]]; then
        echo "[5/6] Generating package..."
        cmake --build "$BUILD_DIR" --target package
        echo ""
        echo "Packages:"
        find "$BUILD_DIR" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' -o -name '*.tar.xz' -o -name '*.tar.gz' \) -exec basename {} \; 2>/dev/null || true
    else
        echo "[5/6] Package skipped — only available for release builds"
    fi
else
    echo "[5/6] Package skipped — use -p to generate installer"
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo "[6/6] Build complete"
echo ""

# Print artifact locations
EXE_PATH=""
for candidate in \
    "$BUILD_DIR/Vora" \
    "$BUILD_DIR/debug/Vora" \
    "$BUILD_DIR/release/Vora" \
    "$BUILD_DIR/Debug/Vora" \
    "$BUILD_DIR/Release/Vora"; do
    if [[ -f "$candidate" ]]; then
        EXE_PATH="$candidate"
        echo "  Executable  : $EXE_PATH"
        break
    fi
done

LIB_PATH="$BUILD_DIR/libvora_lib.a"
if [[ -f "$BUILD_DIR/vora_lib.a" ]]; then
    echo "  Static lib  : $BUILD_DIR/vora_lib.a"
elif [[ -f "$BUILD_DIR/libvora_lib.a" ]]; then
    echo "  Static lib  : $BUILD_DIR/libvora_lib.a"
fi

echo ""
echo "==== Build Success ===="

# Show available presets hint
echo ""
echo "Run without arguments for interactive mode"
echo "Usage  : $0 [-a ARCH] [-c CONFIG] [-p] [-C] [-- vora args...]"
