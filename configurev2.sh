#!/bin/bash

# ReactOS Simplified Configure Script
# This script creates build directory and runs CMake with settings from ReactOS.cmake

set -e

REACTOS_SOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
    cat << EOF
Usage: $0 [options]

Options:
    -h, --help              Show this help message
    -a, --arch ARCH         Set architecture (i386, amd64, arm, arm64)
    -t, --type TYPE         Set build type (Debug, Release, MinSizeRel, RelWithDebInfo)
    -g, --generator GEN     Set CMake generator (Ninja, "Unix Makefiles")
    -o, --output DIR        Set output directory name
    -p, --toolchain-path    Set toolchain binaries path (e.g., /home/$(whoami)/toolchains/x86_64-w64-mingw32/bin)
    --toolchain-prefix      Set toolchain prefix (e.g., x86_64-w64-mingw32)
    --clang                 Configure using the Clang toolchain file
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
            shift 2
            ;;
        --toolchain-prefix)
            TOOLCHAIN_PREFIX="$2"
            shift 2
            ;;
        --clang)
            USE_CLANG=1
            TOOLCHAIN_FILE="toolchain-clang.cmake"
            shift
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

if [ -z "$ARCH" ] && [ -n "$ROS_ARCH" ]; then
    ARCH="$ROS_ARCH"
fi

[ -z "$ARCH" ] && ARCH="i386"
[ -z "$BUILD_TYPE" ] && BUILD_TYPE="RelWithDebInfo"
[ -z "$CMAKE_GENERATOR" ] && CMAKE_GENERATOR="Ninja"
[ -z "$ENABLE_CCACHE" ] && ENABLE_CCACHE="OFF"
if [ -z "$TOOLCHAIN_PATH" ]; then
    case "$ARCH" in
        amd64|x86_64)
            TOOLCHAIN_PATH="/home/$(whoami)/toolchains/x86_64-w64-mingw32/bin"
            ;;
        arm64|aarch64)
            TOOLCHAIN_PATH="/home/$(whoami)/toolchains/aarch64-w64-mingw32/bin"
            ;;
        i386|x86)
            TOOLCHAIN_PATH="/home/$(whoami)/toolchains/i686-w64-mingw32/bin"
            ;;
        *)
            TOOLCHAIN_PATH="/home/$(whoami)/toolchains/i686-w64-mingw32/bin"
            ;;
    esac
fi

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
    esac
fi

ARCH_LOWER=$(echo "$ARCH" | tr '[:upper:]' '[:lower:]')

if [ -z "$OUTPUT_DIR" ]; then
    if [ "$USE_CLANG" -eq 1 ]; then
        OUTPUT_DIR="output-Clang-${ARCH_LOWER}"
    else
        OUTPUT_DIR="output-MinGW-${ARCH_LOWER}"
    fi
fi

echo "========================================="
echo "ReactOS Build Configuration"
echo "========================================="
echo "Source Directory: $REACTOS_SOURCE_DIR"
echo "Output Directory: $OUTPUT_DIR"
echo "Architecture:     $ARCH"
echo "Build Type:       $BUILD_TYPE"
echo "Generator:        $CMAKE_GENERATOR"
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
CMAKE_COMMAND="cmake -G \"$CMAKE_GENERATOR\" \
    -DCMAKE_BUILD_TYPE=\"$BUILD_TYPE\" \
    -DARCH=\"$ARCH\" \
    -DTOOLCHAIN_PATH=\"$TOOLCHAIN_PATH\" \
    -DTOOLCHAIN_PREFIX=\"$TOOLCHAIN_PREFIX\" \
    -DENABLE_CCACHE:BOOL=\"$ENABLE_CCACHE\" \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=\"$REACTOS_SOURCE_DIR/$TOOLCHAIN_FILE\" \
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
        echo "  cd $OUTPUT_DIR && ninja lviecd"
    else
        echo "  cd $OUTPUT_DIR && make"
        echo "  cd $OUTPUT_DIR && make lviecd"
    fi
    echo "========================================="
else
    echo
    echo "ERROR: CMake configuration failed!"
    exit 1
fi
