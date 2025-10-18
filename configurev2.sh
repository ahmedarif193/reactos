#!/bin/bash

# ReactOS Simplified Configure Script
# This script creates build directory and runs CMake with settings from ReactOS.cmake

set -e

REACTOS_SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
DEFAULT_TOOLCHAIN_ROOT="${HOME}/mingw-toolchains"
MINGW_X86_64_URL="https://github.com/ahmedarif193/mingw-gcc15.2/releases/download/v15.2/x86_64-w64-mingw32.tar.gz"
MINGW_I686_URL="https://github.com/ahmedarif193/mingw-gcc15.2/releases/download/v15.2/i686-w64-mingw32.tar.gz"
MINGW_TOOLCHAIN_BANNER_SHOWN=0

ensure_mingw_toolchain() {
    local prefix="$1"
    local url="$2"
    local target_dir="${DEFAULT_TOOLCHAIN_ROOT}/${prefix}"
    local bin_dir="${target_dir}/bin"

    if [ -z "$prefix" ] || [ -z "$url" ]; then
        return
    fi

    if [ -d "$bin_dir" ]; then
        return
    fi

    if [ "$MINGW_TOOLCHAIN_BANNER_SHOWN" -eq 0 ]; then
        echo "========================================="
        echo "MinGW toolchains not found in ${DEFAULT_TOOLCHAIN_ROOT}"
        echo "Downloading required toolchains..."
        echo "========================================="
        MINGW_TOOLCHAIN_BANNER_SHOWN=1
    fi

    if ! command -v curl >/dev/null 2>&1; then
        echo "Error: curl is required to download MinGW toolchains." >&2
        exit 1
    fi

    mkdir -p "$DEFAULT_TOOLCHAIN_ROOT"

    local archive_name="$(basename "$url")"
    local archive_path="/tmp/${archive_name}"

    echo "Fetching ${archive_name}..."
    curl -L --fail "$url" -o "$archive_path"

    echo "Extracting ${archive_name} into ${DEFAULT_TOOLCHAIN_ROOT}..."
    tar -xzf "$archive_path" -C "$DEFAULT_TOOLCHAIN_ROOT"

    rm -f "$archive_path"
}

usage() {
    cat << EOF
Usage: $0 [options]

Options:
    -h, --help              Show this help message
    -a, --arch ARCH         Set architecture (i386, amd64, arm, arm64)
    -t, --type TYPE         Set build type (Debug, Release, MinSizeRel, RelWithDebInfo)
    -r, --release           Shortcut for --type Release
    -g, --generator GEN     Set CMake generator (Ninja, "Unix Makefiles")
    -o, --output DIR        Set output directory name
    -p, --toolchain-path    Set toolchain binaries path (e.g., $HOME/mingw-toolchains/x86_64-w64-mingw32/bin)
    --toolchain-prefix      Set toolchain prefix (e.g., x86_64-w64-mingw32)
    --clang[=VER]           Configure using the Clang toolchain file (defaults to clang-19)
    --clang-version VER     Use clang binaries with the specified version suffix (e.g., 18)
    -c, --ccache            Enable ccache
    --clean                 Clean build directory before configuring
    
Additional CMake options can be passed with -D flags, e.g.:
    $0 -DENABLE_ROSTESTS=1

If no options are provided, defaults from ReactOS.cmake will be used.
EOF
    exit 0
}

ARCH="amd64"
BUILD_TYPE="Debug"
CMAKE_GENERATOR=""
OUTPUT_DIR=""
TOOLCHAIN_PATH=""
USER_PROVIDED_TOOLCHAIN_PATH=0
TOOLCHAIN_PREFIX=""
TOOLCHAIN_FILE="toolchain-gcc.cmake"
ENABLE_CCACHE=""
CLEAN_BUILD=0
CMAKE_EXTRA_ARGS=""
USE_CLANG=0
CLANG_VERSION=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            ;;
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -g|--generator)
            CMAKE_GENERATOR="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -p|--toolchain-path)
            TOOLCHAIN_PATH="$2"
            USER_PROVIDED_TOOLCHAIN_PATH=1
            shift 2
            ;;
        --toolchain-prefix)
            TOOLCHAIN_PREFIX="$2"
            shift 2
            ;;
        --clang=*)
            USE_CLANG=1
            TOOLCHAIN_FILE="toolchain-clang.cmake"
            CLANG_VERSION="${1#*=}"
            shift
            ;;
        --clang)
            USE_CLANG=1
            TOOLCHAIN_FILE="toolchain-clang.cmake"
            if [ -n "$2" ] && [[ "$2" != -* ]]; then
                CLANG_VERSION="$2"
                shift 2
            else
                shift
            fi
            ;;
        --clang-version)
            if [ -z "$2" ] || [[ "$2" == -* ]]; then
                echo "Error: --clang-version requires a value"
                exit 1
            fi
            CLANG_VERSION="$2"
            USE_CLANG=1
            TOOLCHAIN_FILE="toolchain-clang.cmake"
            shift 2
            ;;
        -c|--ccache)
            ENABLE_CCACHE="ON"
            shift
            ;;
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        -D*)
            CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS $1"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

if [ "$USE_CLANG" -eq 1 ] && [ -z "$CLANG_VERSION" ]; then
    CLANG_VERSION="19"
fi

if [ -z "$ARCH" ] && [ -n "$ROS_ARCH" ]; then
    ARCH="$ROS_ARCH"
fi

#make ENABLE_CCACHE the default behavior, ccache became robust since years (2025 update) 
[ -z "$ARCH" ] && ARCH="i386"
[ -z "$BUILD_TYPE" ] && BUILD_TYPE="RelWithDebInfo"
[ -z "$CMAKE_GENERATOR" ] && CMAKE_GENERATOR="Ninja"
[ -z "$ENABLE_CCACHE" ] && ENABLE_CCACHE="OFF"
if [ -z "$TOOLCHAIN_PREFIX" ]; then
    case "$ARCH" in
        amd64|x86_64)
            TOOLCHAIN_PREFIX="x86_64-w64-mingw32"
            ;;
        i386|x86)
            TOOLCHAIN_PREFIX="i686-w64-mingw32"
            ;;
        arm)
            TOOLCHAIN_PREFIX="arm-w64-mingw32"
            ;;
        arm64|aarch64)
            TOOLCHAIN_PREFIX="aarch64-w64-mingw32"
            ;;
        *)
            TOOLCHAIN_PREFIX="i686-w64-mingw32"
            ;;
    esac
fi

if [ -z "$TOOLCHAIN_PATH" ]; then
    case "$TOOLCHAIN_PREFIX" in
        x86_64-w64-mingw32)
            TOOLCHAIN_PATH="${DEFAULT_TOOLCHAIN_ROOT}/x86_64-w64-mingw32/bin"
            ;;
        i686-w64-mingw32)
            TOOLCHAIN_PATH="${DEFAULT_TOOLCHAIN_ROOT}/i686-w64-mingw32/bin"
            ;;
        arm-w64-mingw32)
            TOOLCHAIN_PATH="${DEFAULT_TOOLCHAIN_ROOT}/arm-w64-mingw32/bin"
            ;;
        aarch64-w64-mingw32)
            TOOLCHAIN_PATH="${DEFAULT_TOOLCHAIN_ROOT}/aarch64-w64-mingw32/bin"
            ;;
        *)
            TOOLCHAIN_PATH="${DEFAULT_TOOLCHAIN_ROOT}/i686-w64-mingw32/bin"
            ;;
    esac
fi

if [ "$USER_PROVIDED_TOOLCHAIN_PATH" -eq 0 ]; then
    ensure_mingw_toolchain "x86_64-w64-mingw32" "$MINGW_X86_64_URL"
    ensure_mingw_toolchain "i686-w64-mingw32" "$MINGW_I686_URL"
fi

ARCH_LOWER=$(echo "$ARCH" | tr '[:upper:]' '[:lower:]')

if [ -z "$OUTPUT_DIR" ]; then
    if [ "$USE_CLANG" -eq 1 ]; then
        OUTPUT_DIR="output-Clang-${ARCH_LOWER}"
    else
        OUTPUT_DIR="output-MinGW-${ARCH_LOWER}"
    fi
fi

BUILD_TYPE_SANITIZED="${BUILD_TYPE// /_}"
OUTPUT_DIR="${OUTPUT_DIR}-${BUILD_TYPE_SANITIZED}"

echo "========================================="
echo "ReactOS Build Configuration"
echo "========================================="
echo "Source Directory: $REACTOS_SOURCE_DIR"
echo "Output Directory: $OUTPUT_DIR"
echo "Architecture:     $ARCH"
echo "Build Type:       $BUILD_TYPE"
echo "Generator:        $CMAKE_GENERATOR"
if [ "$USE_CLANG" -eq 1 ]; then
    echo "Build Environment: Clang (GCC toolchain support)"
else
    echo "Build Environment: MinGW"
fi
echo "Toolchain Path:   $TOOLCHAIN_PATH"
echo "Toolchain Prefix: $TOOLCHAIN_PREFIX"
echo "Toolchain File:   $TOOLCHAIN_FILE"
if [ "$USE_CLANG" -eq 1 ] && [ -n "$CLANG_VERSION" ]; then
    echo "Clang Version:   $CLANG_VERSION"
fi
echo "Enable ccache:    $ENABLE_CCACHE"
if [ -n "$CMAKE_EXTRA_ARGS" ]; then
    echo "Extra CMake args: $CMAKE_EXTRA_ARGS"
fi
echo "========================================="
echo

if [ "$CLEAN_BUILD" -eq 1 ] && [ -d "$OUTPUT_DIR" ]; then
    echo "Cleaning existing build directory..."
    rm -rf "$OUTPUT_DIR"
fi

echo "Creating build directory: $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

# Ensure desired toolchain binaries are picked up by CMake's find_program.
if [ -d "$TOOLCHAIN_PATH" ]; then
    export PATH="$TOOLCHAIN_PATH:$PATH"
fi

if [ -f "CMakeCache.txt" ]; then
    echo "Removing existing CMakeCache.txt..."
    rm -f CMakeCache.txt
fi
if [ -f "host-tools/CMakeCache.txt" ]; then
    rm -f host-tools/CMakeCache.txt
fi

echo "Running CMake configuration..."
echo

NINJA_PATH=""
if [ "$CMAKE_GENERATOR" = "Ninja" ]; then
    if command -v ninja >/dev/null 2>&1; then
        NINJA_PATH=$(which ninja)
    elif command -v ninja-build >/dev/null 2>&1; then
        NINJA_PATH=$(which ninja-build)
    fi
    
    if [ -n "$NINJA_PATH" ]; then
        CMAKE_MAKE_PROGRAM_ARG="-DCMAKE_MAKE_PROGRAM=\"$NINJA_PATH\""
    fi
fi

CLANG_VERSION_ARG=""
if [ -n "$CLANG_VERSION" ]; then
    CLANG_VERSION_ARG="-DCLANG_VERSION=\"$CLANG_VERSION\""
fi

if [ "$USE_CLANG" -eq 1 ]; then
    BUILD_ENVIRONMENT_VALUE="Clang"
else
    BUILD_ENVIRONMENT_VALUE="MinGW"
fi

CMAKE_COMMAND="cmake -G \"$CMAKE_GENERATOR\" \
    -DCMAKE_BUILD_TYPE=\"$BUILD_TYPE\" \
    -DARCH=\"$ARCH\" \
    -DTOOLCHAIN_PATH=\"$TOOLCHAIN_PATH\" \
    -DTOOLCHAIN_PREFIX=\"$TOOLCHAIN_PREFIX\" \
    -DENABLE_CCACHE:BOOL=\"$ENABLE_CCACHE\" \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=\"$REACTOS_SOURCE_DIR/$TOOLCHAIN_FILE\" \
    -DBUILD_ENVIRONMENT=\"$BUILD_ENVIRONMENT_VALUE\" \
    ${CMAKE_MAKE_PROGRAM_ARG} \
    -C \"$REACTOS_SOURCE_DIR/ReactOS.cmake\" \
    ${CLANG_VERSION_ARG} \
    $CMAKE_EXTRA_ARGS \
    \"$REACTOS_SOURCE_DIR\""

echo "Executing: $CMAKE_COMMAND"
echo

eval $CMAKE_COMMAND

if [ $? -eq 0 ]; then
    echo
    echo "========================================="
    echo "Configuration successful!"
    echo "========================================="
    echo "Build directory: $OUTPUT_DIR"
    echo
    echo "To build ReactOS, run one of:"
    if [ "$CMAKE_GENERATOR" = "Ninja" ]; then
        echo "  cd $OUTPUT_DIR && ninja"
        echo "  cd $OUTPUT_DIR && ninja livecd"
    else
        echo "  cd $OUTPUT_DIR && make"
        echo "  cd $OUTPUT_DIR && make livecd"
    fi
    echo "========================================="
else
    echo
    echo "ERROR: CMake configuration failed!"
    exit 1
fi
