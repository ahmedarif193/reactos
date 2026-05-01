#!/bin/sh

REACTOS_SOURCE_DIR=$(cd "$(dirname "$0")" && pwd)

# RosBE installation precedence:
#   1. ROSBE_DOCKER_ACTIVE=1 (set by `rosbe enable` from the docker bootstrap):
#      use the rosbe-builder container, paths at /opt/rosbe/*. The output
#      dir gets a "-docker" suffix as a reminder that this build tree only
#      works in an `rosbe enable`-d shell.
#   2. ~/.local/opt/rosbe (the host-installed bootstrap):
#      use local toolchain, validate -x as before.
#   3. Neither: print install instructions and exit.
ROSBE_OUTPUT_SUFFIX=""
if [ "${ROSBE_DOCKER_ACTIVE:-0}" = "1" ]; then
	ROSBE_ROOT="/opt/rosbe"
	ROSBE_SKIP_HOST_CHECK=1
	ROSBE_OUTPUT_SUFFIX="-docker"
elif [ -d "$HOME/.local/opt/rosbe/llvm-mingw" ] || [ -d "$HOME/.local/opt/rosbe/mingw-gcc" ]; then
	ROSBE_ROOT="$HOME/.local/opt/rosbe"
	ROSBE_SKIP_HOST_CHECK=0
else
	cat >&2 <<'NO_ROSBE'
configure.sh: no RosBE installation found.

Install one of:

  - Local RosBE (compiles run on the host):
      curl -fsSL https://raw.githubusercontent.com/ahmedarif193/winget-rosbe/main/rosbe-linux-bootstrap.sh | sh

  - Docker RosBE (compiles run in a rootless container):
      curl -fsSL https://raw.githubusercontent.com/ahmedarif193/winget-rosbe/main/rosbe-linux-docker-bootstrap.sh | sh
      # then open a new shell and:
      rosbe enable

Then re-run configure.sh.
NO_ROSBE
	exit 1
fi
ROSBE_LLVM_ROOT="$ROSBE_ROOT/llvm-mingw"
ROSBE_GCC_ROOT="$ROSBE_ROOT/mingw-gcc"

CMAKE_GENERATOR="Ninja"
USE_CLANG=0
ARCH=amd64
BUILD_TYPE=Debug
BUILD_TYPE_SUFFIX=debug
ROS_CMAKEOPTS=
USER_BUILD_TYPE=0

usage() {
	echo "Usage: configure.sh [options]"
	echo "  --clang              Use Clang/LLVM from ~/.local/opt/rosbe/llvm-mingw"
	echo "  -a, --arch <arch>    Target architecture: amd64, i386, arm64 (default: amd64)"
	echo "  -r, --release        Configure a Release build (default: Debug)"
	echo "  makefiles            Use Unix Makefiles generator (default: Ninja)"
	echo "  -D<var>=<val>        Pass option to CMake"
	exit 1
}

fail() {
	echo "configure.sh: $*" >&2
	exit 1
}

lower_build_type() {
	case "$1" in
		Release|release)
			echo release
			;;
		Debug|debug|"")
			echo debug
			;;
		*)
			printf '%s\n' "$1" | tr '[:upper:]' '[:lower:]'
			;;
	esac
}

normalize_arch() {
	case "$1" in
		amd64|x64|x86_64)
			echo amd64
			;;
		i386|x86)
			echo i386
			;;
		arm64|aarch64)
			echo arm64
			;;
		arm)
			echo arm
			;;
		*)
			fail "unsupported architecture: $1"
			;;
	esac
}

gcc_triplet_for_arch() {
	case "$1" in
		amd64)
			echo x86_64-w64-mingw32
			;;
		i386)
			echo i686-w64-mingw32
			;;
		arm64)
			echo aarch64-w64-mingw32
			;;
		arm)
			echo arm-mingw32ce
			;;
		*)
			fail "unsupported architecture: $1"
			;;
	esac
}

target_windmc_for_arch() {
	case "$1" in
		amd64|i386)
			# In container mode the binary lives in the image; skip the host -x test.
			if [ "$ROSBE_SKIP_HOST_CHECK" = "1" ]; then
				echo "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-windmc"
				return
			fi
			if [ -x "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-windmc" ]; then
				echo "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-windmc"
				return
			fi
			;;
	esac

	return 1
}

host_windmc() {
	if [ "$ROSBE_SKIP_HOST_CHECK" = "1" ]; then
		echo "$ROSBE_GCC_ROOT/x86_64-w64-mingw32/bin/x86_64-w64-mingw32-windmc"
		return
	fi
	for candidate in \
		"$ROSBE_GCC_ROOT/x86_64-w64-mingw32/bin/x86_64-w64-mingw32-windmc" \
		"$ROSBE_GCC_ROOT/i686-w64-mingw32/bin/i686-w64-mingw32-windmc"
	do
		if [ -x "$candidate" ]; then
			echo "$candidate"
			return
		fi
	done

	return 1
}

remember_cmake_arg() {
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" $1"
	case "$1" in
		-DCMAKE_BUILD_TYPE=*|CMAKE_BUILD_TYPE=*|-DCMAKE_BUILD_TYPE:*=*|CMAKE_BUILD_TYPE:*=*)
			BUILD_TYPE=${1#*=}
			BUILD_TYPE_SUFFIX=$(lower_build_type "$BUILD_TYPE")
			USER_BUILD_TYPE=1
			;;
	esac
}

while [ $# -gt 0 ]; do
	case "$1" in
		--help|-h)
			usage
			;;
		--clang|clang|Clang)
			USE_CLANG=1
			;;
		-a|--arch)
			shift
			[ $# -gt 0 ] || fail "missing architecture after -a/--arch"
			ARCH=$(normalize_arch "$1")
			;;
		--arch=*)
			ARCH=$(normalize_arch "${1#--arch=}")
			;;
		-r|--release)
			BUILD_TYPE=Release
			BUILD_TYPE_SUFFIX=release
			;;
		makefiles|Makefiles)
			CMAKE_GENERATOR="Unix Makefiles"
			;;
		-D)
			shift
			[ $# -gt 0 ] || fail "missing CMake argument after -D"
			case "$1" in
				?*=*)
					remember_cmake_arg "-D$1"
					;;
				*)
					usage
					;;
			esac
			;;
		-D?*=*|-D?*)
			remember_cmake_arg "$1"
			;;
		*)
			usage
			;;
	esac

	shift
done

if [ "$USER_BUILD_TYPE" -eq 0 ]; then
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DCMAKE_BUILD_TYPE:STRING=$BUILD_TYPE"
fi

GCC_TRIPLET=$(gcc_triplet_for_arch "$ARCH")
GCC_TOOLCHAIN_ROOT="$ROSBE_GCC_ROOT/$GCC_TRIPLET"

if [ "$USE_CLANG" -eq 1 ]; then
	BUILD_ENVIRONMENT=Clang
	TOOLCHAIN_FILE=toolchain-clang.cmake

	if [ "$ROSBE_SKIP_HOST_CHECK" != "1" ]; then
		[ -x "$ROSBE_LLVM_ROOT/bin/clang" ] || fail "missing RosBE LLVM toolchain at $ROSBE_LLVM_ROOT"
		[ -x "$ROSBE_LLVM_ROOT/bin/clang++" ] || fail "missing RosBE LLVM clang++ at $ROSBE_LLVM_ROOT/bin"
	fi
	MC_COMPILER=$(target_windmc_for_arch "$ARCH" || host_windmc) || fail "missing RosBE windmc in $ROSBE_GCC_ROOT"

	export REACTOS_CLANG_LLVM_MINGW_ROOT="$ROSBE_LLVM_ROOT"
	export LLVM_MINGW_ROOT="$ROSBE_LLVM_ROOT"
	export PATH="$ROSBE_LLVM_ROOT/bin:$PATH"

	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=$ROSBE_LLVM_ROOT"
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DCMAKE_MC_COMPILER:FILEPATH=$MC_COMPILER"
	if [ "$ROSBE_SKIP_HOST_CHECK" = "1" ] || [ -d "$GCC_TOOLCHAIN_ROOT/bin" ]; then
		export PATH="$GCC_TOOLCHAIN_ROOT/bin:$PATH"
		ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DREACTOS_CLANG_GCC_TOOLCHAIN:PATH=$GCC_TOOLCHAIN_ROOT"
	fi
else
	BUILD_ENVIRONMENT=GCC
	TOOLCHAIN_FILE=toolchain-gcc.cmake

	if [ "$ROSBE_SKIP_HOST_CHECK" != "1" ]; then
		[ -x "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-gcc" ] || fail "missing RosBE GCC toolchain for $ARCH at $GCC_TOOLCHAIN_ROOT"
		[ -x "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-g++" ] || fail "missing RosBE GCC g++ for $ARCH at $GCC_TOOLCHAIN_ROOT/bin"
	fi
	MC_COMPILER=$(target_windmc_for_arch "$ARCH") || fail "missing RosBE windmc for $ARCH at $GCC_TOOLCHAIN_ROOT/bin"

	export PATH="$GCC_TOOLCHAIN_ROOT/bin:$PATH"
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DCMAKE_MC_COMPILER:FILEPATH=$MC_COMPILER"
fi

REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH-$BUILD_TYPE_SUFFIX$ROSBE_OUTPUT_SUFFIX
BUILD_HINT_PATH="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH"

echo "Configuring a new ReactOS build on:"
uname -srvpio
echo
echo "RosBE root:    $ROSBE_ROOT"
if [ "$ROSBE_SKIP_HOST_CHECK" = "1" ]; then
	echo "RosBE mode:    container (${ROSBE_DOCKER_IMAGE:-rosbe-builder})"
fi
echo "Compiler:      $BUILD_ENVIRONMENT"
echo "Architecture:  $ARCH"
echo "Build type:    $BUILD_TYPE"
echo "Generator:     $CMAKE_GENERATOR"
echo "Output path:   $REACTOS_OUTPUT_PATH"
echo

if [ "$REACTOS_SOURCE_DIR" = "$PWD" ]; then
	BUILD_HINT_PATH="./$REACTOS_OUTPUT_PATH"
	echo "Creating directories in $REACTOS_OUTPUT_PATH"
	mkdir -p "$REACTOS_OUTPUT_PATH"
	cd "$REACTOS_OUTPUT_PATH" || exit 1
fi

rm -f CMakeCache.txt host-tools/CMakeCache.txt

cmake -G "$CMAKE_GENERATOR" \
	-DENABLE_CCACHE:BOOL=0 \
	-DCMAKE_TOOLCHAIN_FILE:FILEPATH="$TOOLCHAIN_FILE" \
	-DARCH:STRING="$ARCH" \
	$ROS_CMAKEOPTS \
	"$REACTOS_SOURCE_DIR"
if [ $? -ne 0 ]; then
	echo "An error occurred while configuring ReactOS"
	exit 1
fi

if [ "$CMAKE_GENERATOR" = "Unix Makefiles" ]; then
	BUILD_TOOL=make
else
	BUILD_TOOL=ninja
fi

echo "========================================"
echo "Configure script complete."
echo "Build the LiveCD with:"
printf '  cd %s && %s livecd\n' "$BUILD_HINT_PATH" "$BUILD_TOOL"
echo "========================================"
