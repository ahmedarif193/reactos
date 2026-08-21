#!/bin/sh
# menuconfig.sh - interactive ReactOS build configuration (OpenWrt/kconfig
# style). The host tool is built in the source tree, while selections and the
# generated CMake fragment are owned by one output tree.

REACTOS_SOURCE_DIR=$(cd "$(dirname "$0")" && pwd -P)
REACTOS_START_DIR=$(pwd -P)
REACTOS_DEFAULT_BUILD_DIR="$REACTOS_SOURCE_DIR/output-Clang-amd64-debug"

ROSCONFIG_DIR="$REACTOS_SOURCE_DIR/.rosconfig"
ROSCONFIG_BIN="$ROSCONFIG_DIR/rosconfig"
ROSCONFIG_BUILD="$REACTOS_SOURCE_DIR/sdk/tools/rosconfig/build.sh"
ROSCONFIG_DEF="$REACTOS_SOURCE_DIR/sdk/cmake/rosconfig.def"

fail() {
	echo "menuconfig.sh: $*" >&2
	exit 1
}

usage() {
	echo "Usage: ./menuconfig.sh [--build-dir <output-directory>] [--self-test]" >&2
	echo "Default build directory: $REACTOS_DEFAULT_BUILD_DIR" >&2
	exit 2
}

rosconfig_file_get() {
	[ -f "$1" ] || return 0
	sed -n "s/^$2=//p" "$1" | head -n 1
}

cmake_cache_get() {
	[ -f "$1" ] || return 0
	sed -n "s/^$2:[^=]*=//p" "$1" | head -n 1
}

infer_build_identity() {
	INFERRED_ARCH=
	INFERRED_BUILD_TYPE=
	INFERRED_TOOLCHAIN=
	BUILD_TARGET=

	BUILD_NAME=$(basename "$1")
	BUILD_NAME=${BUILD_NAME%-docker}
	BUILD_NAME=${BUILD_NAME%-sln}
	case "$BUILD_NAME" in
		output-Clang-*)
			INFERRED_TOOLCHAIN=clang
			BUILD_TARGET=${BUILD_NAME#output-Clang-}
			;;
		output-GCC-*|output-MinGW-*)
			INFERRED_TOOLCHAIN=gcc
			BUILD_TARGET=${BUILD_NAME#output-GCC-}
			BUILD_TARGET=${BUILD_TARGET#output-MinGW-}
			;;
		output-VS-*|output-MSVC-*)
			INFERRED_TOOLCHAIN=msvc
			BUILD_TARGET=${BUILD_NAME#output-VS-}
			BUILD_TARGET=${BUILD_TARGET#output-MSVC-}
			;;
		*)
			return
			;;
	esac

	case "$BUILD_TARGET" in
		*-debug)
			INFERRED_ARCH=${BUILD_TARGET%-debug}
			INFERRED_BUILD_TYPE=Debug
			;;
		*-release)
			INFERRED_ARCH=${BUILD_TARGET%-release}
			INFERRED_BUILD_TYPE=Release
			;;
	esac
	case "$INFERRED_ARCH" in
		amd64|i386|arm64|arm) ;;
		*)
			INFERRED_ARCH=
			INFERRED_BUILD_TYPE=
			INFERRED_TOOLCHAIN=
			;;
	esac
}

[ -x "$ROSCONFIG_BUILD" ] || fail "missing $ROSCONFIG_BUILD"
"$ROSCONFIG_BUILD" "$ROSCONFIG_DIR" "$ROSCONFIG_BIN" || fail "failed to build the rosconfig tool"

if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
	exec "$ROSCONFIG_BIN" --self-test
fi
[ "$#" -eq 0 ] || [ "$#" -eq 2 ] || usage

if [ "$#" -eq 2 ]; then
	[ "$1" = "--build-dir" ] || usage
	case "$2" in
		/*) BUILD_DIR=$2 ;;
		*) BUILD_DIR="$REACTOS_START_DIR/$2" ;;
	esac
else
	case "$(basename "$REACTOS_START_DIR")" in
		output-*) BUILD_DIR=$REACTOS_START_DIR ;;
		*) BUILD_DIR=$REACTOS_DEFAULT_BUILD_DIR ;;
	esac
fi

[ ! -e "$BUILD_DIR" ] || [ -d "$BUILD_DIR" ] || fail "build path is not a directory: $BUILD_DIR"
if [ -d "$BUILD_DIR" ]; then
	BUILD_DIR=$(cd "$BUILD_DIR" && pwd -P)
fi
[ "$BUILD_DIR" != "$REACTOS_SOURCE_DIR" ] || fail "run from an output directory or pass --build-dir output-<toolchain>-<arch>-<type>"

ROSCONFIG_STATE_DIR="$BUILD_DIR/.rosconfig"
ROSCONFIG_CACHE="$ROSCONFIG_STATE_DIR/config.cache"
ROSCONFIG_OVERRIDES="$ROSCONFIG_STATE_DIR/overrides.cmake"
CMAKE_CACHE="$BUILD_DIR/CMakeCache.txt"

infer_build_identity "$BUILD_DIR"

ARCH=$(cmake_cache_get "$CMAKE_CACHE" ARCH)
[ -n "$ARCH" ] || ARCH=$(rosconfig_file_get "$ROSCONFIG_CACHE" ARCH)
[ -n "$ARCH" ] || ARCH=$INFERRED_ARCH
case "$ARCH" in
	amd64|i386|arm64|arm) ;;
	*) fail "cannot determine a valid target architecture for $BUILD_DIR" ;;
esac

BUILD_TYPE=$(cmake_cache_get "$CMAKE_CACHE" CMAKE_BUILD_TYPE)
[ -n "$BUILD_TYPE" ] || BUILD_TYPE=$(rosconfig_file_get "$ROSCONFIG_CACHE" BUILD_TYPE)
[ -n "$BUILD_TYPE" ] || BUILD_TYPE=$INFERRED_BUILD_TYPE
case "$BUILD_TYPE" in
	Debug|Release) ;;
	*) fail "cannot determine Debug or Release configuration for $BUILD_DIR" ;;
esac

TOOLCHAIN_FILE=$(cmake_cache_get "$CMAKE_CACHE" CMAKE_TOOLCHAIN_FILE)
case "$TOOLCHAIN_FILE" in
	*toolchain-clang.cmake) TOOLCHAIN=clang ;;
	*toolchain-gcc.cmake) TOOLCHAIN=gcc ;;
	*toolchain-msvc.cmake) TOOLCHAIN=msvc ;;
	*) TOOLCHAIN=$(rosconfig_file_get "$ROSCONFIG_CACHE" TOOLCHAIN) ;;
esac
[ -n "$TOOLCHAIN" ] || TOOLCHAIN=$INFERRED_TOOLCHAIN
case "$TOOLCHAIN" in
	clang|gcc|msvc) ;;
	*) fail "cannot determine the toolchain for $BUILD_DIR" ;;
esac

if [ ! -d "$BUILD_DIR" ]; then
	echo "Creating output configuration directory: $BUILD_DIR"
	mkdir -p "$BUILD_DIR" || fail "could not create output directory: $BUILD_DIR"
	BUILD_DIR=$(cd "$BUILD_DIR" && pwd -P)
	ROSCONFIG_STATE_DIR="$BUILD_DIR/.rosconfig"
	ROSCONFIG_CACHE="$ROSCONFIG_STATE_DIR/config.cache"
	ROSCONFIG_OVERRIDES="$ROSCONFIG_STATE_DIR/overrides.cmake"
fi

mkdir -p "$ROSCONFIG_STATE_DIR"
"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE" || fail "could not initialize $ROSCONFIG_CACHE"

"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --menu
menu_status=$?
if [ "$menu_status" -ne 0 ]; then
	[ "$menu_status" -eq 130 ] && echo "menuconfig.sh: cancelled; configuration was not regenerated." >&2
	exit "$menu_status"
fi

# The output directory fixes these identity values; menu changes apply only to
# the configuration owned by this tree.
"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE" || fail "could not write $ROSCONFIG_CACHE"
"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --generate "$ROSCONFIG_OVERRIDES" || fail "could not write $ROSCONFIG_OVERRIDES"

echo ""
echo "Configuration stored in $ROSCONFIG_CACHE."
echo "Re-run configure.sh for this output tree to apply these settings"
echo "(command-line flags and -D options still take precedence)."
