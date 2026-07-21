#!/bin/sh

REACTOS_SOURCE_DIR=$(cd "$(dirname "$0")" && pwd)

# rosconfig (menuconfig) support: a small host tool keeps persistent build
# options in an untracked cache and turns them into a CMake pre-load file
# that /PreLoad.cmake includes. See sdk/tools/rosconfig/README.md.
ROSCONFIG_DIR="$REACTOS_SOURCE_DIR/.rosconfig"
ROSCONFIG_BIN="$ROSCONFIG_DIR/rosconfig"
ROSCONFIG_BUILD="$REACTOS_SOURCE_DIR/sdk/tools/rosconfig/build.sh"
ROSCONFIG_DEF="$REACTOS_SOURCE_DIR/sdk/cmake/rosconfig.def"
ROSCONFIG_CACHE="$ROSCONFIG_DIR/config.cache"
ROSCONFIG_OVERRIDES="$ROSCONFIG_DIR/overrides.cmake"
ROSCONFIG_OK=0
ROSCONFIG_EXIT_SKIP_CONFIGURE=3

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

CMAKE_GENERATOR="Ninja"
USE_CLANG=1
ARCH=amd64
BUILD_TYPE=Debug
BUILD_TYPE_SUFFIX=debug
ROS_CMAKEOPTS=
USER_BUILD_TYPE=0
USER_ARCH=0
USER_TOOLCHAIN=0
USER_BUILD_TYPE_FLAG=0
RUN_MENUCONFIG=0

usage() {
	echo "Usage: configure.sh [options]"
	echo "  --clang              Use Clang/LLVM from ~/.local/opt/rosbe/llvm-mingw (default)"
	echo "  --gcc                Use GCC from ~/.local/opt/rosbe/mingw-gcc"
	echo "  -a, --arch <arch>    Target architecture: amd64, i386, arm64 (default: amd64)"
	echo "  -r, --release        Configure a Release build (default: Debug)"
	echo "  makefiles            Use Unix Makefiles generator (default: Ninja)"
	echo "  menuconfig           Open the interactive configuration UI first;"
	echo "                       selections persist in .rosconfig/config.cache"
	echo "  -D<var>=<val>        Pass option to CMake"
	exit 1
}

fail() {
	echo "configure.sh: $*" >&2
	exit 1
}

# Read one raw value from the rosconfig cache (empty output if absent).
rosconfig_cache_get() {
	[ -f "$ROSCONFIG_CACHE" ] || return 0
	sed -n "s/^$1=//p" "$ROSCONFIG_CACHE" | head -n 1
}

# Compile the rosconfig host tool if it is missing or outdated.
rosconfig_build() {
	[ -x "$ROSCONFIG_BUILD" ] || return 1
	if ! "$ROSCONFIG_BUILD" "$ROSCONFIG_DIR" "$ROSCONFIG_BIN"; then
		echo "configure.sh: warning: failed to build the rosconfig tool; menuconfig selections will not be applied." >&2
		rm -f "$ROSCONFIG_BIN"
		return 1
	fi
}

# FEX ARM64EC is opt-in (default OFF, matching sdk/cmake/config.cmake). It is
# enabled when -DENABLE_FEX_ARM64EC=ON (or an equivalent) is passed, or when
# it has been enabled through menuconfig (rosconfig cache).
fex_arm64ec_enabled() {
	case " $ROS_CMAKEOPTS " in
		*" -DENABLE_FEX_ARM64EC=ON "*|*" -DENABLE_FEX_ARM64EC:BOOL=ON "*|*" -DENABLE_FEX_ARM64EC=TRUE "*|*" -DENABLE_FEX_ARM64EC:BOOL=TRUE "*|*" -DENABLE_FEX_ARM64EC=1 "*|*" -DENABLE_FEX_ARM64EC:BOOL=1 "*)
			return 0
			;;
	esac
	[ "$(rosconfig_cache_get ENABLE_FEX_ARM64EC)" = "y" ] && return 0
	return 1
}

sync_arm64_submodules() {
	[ "$ARCH" = "arm64" ] || return 0

	# The FEX submodule (and its recursive gvisor test binaries) is huge; only
	# fetch it when FEX ARM64EC is explicitly enabled.
	if ! fex_arm64ec_enabled; then
		echo "FEX ARM64EC disabled (default); skipping FEX submodule sync."
		echo "  Pass -DENABLE_FEX_ARM64EC=ON to configure.sh to enable it."
		return 0
	fi

	if [ ! -d "$REACTOS_SOURCE_DIR/.git" ]; then
		echo "Skipping ARM64 submodule sync outside a Git checkout."
		return 0
	fi

	command -v git >/dev/null 2>&1 || fail "git is required to initialize ARM64 submodules"

	echo "Syncing FEX ARM64EC submodule..."
	git -C "$REACTOS_SOURCE_DIR" submodule sync -- submodules/fex-arm64ec || fail "failed to sync FEX submodule metadata"
	git -C "$REACTOS_SOURCE_DIR" submodule update --init --recursive -- submodules/fex-arm64ec || fail "failed to initialize FEX submodule"
}

prepare_arm64_fex_source() {
	[ "$ARCH" = "arm64" ] || return 0

	fex_arm64ec_enabled || return 0

	FEX_UPSTREAM_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	FEX_PATCH_FILE="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec-reactos.patch"
	FEX_PREPARED_DIR="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH/submodules/fex-arm64ec-src"

	[ -f "$FEX_UPSTREAM_DIR/CMakeLists.txt" ] || fail "FEX submodule source is missing at $FEX_UPSTREAM_DIR"
	[ -f "$FEX_UPSTREAM_DIR/External/fmt/CMakeLists.txt" ] || fail "FEX submodule dependencies are incomplete"
	[ -f "$FEX_PATCH_FILE" ] || fail "FEX ReactOS patch is missing at $FEX_PATCH_FILE"
	command -v patch >/dev/null 2>&1 || fail "patch is required to prepare the FEX ARM64EC source"

	echo "Preparing FEX ARM64EC source..."
	rm -rf "$FEX_PREPARED_DIR"
	mkdir -p "$(dirname "$FEX_PREPARED_DIR")"
	cp -a "$FEX_UPSTREAM_DIR" "$FEX_PREPARED_DIR"
	rm -rf "$FEX_PREPARED_DIR/.git"
	patch -d "$FEX_PREPARED_DIR" -p1 < "$FEX_PATCH_FILE" >/dev/null || fail "failed to apply FEX ReactOS patch"
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
			USER_TOOLCHAIN=1
			;;
		--gcc|gcc|GCC)
			USE_CLANG=0
			USER_TOOLCHAIN=1
			;;
		-a|--arch)
			shift
			[ $# -gt 0 ] || fail "missing architecture after -a/--arch"
			ARCH=$(normalize_arch "$1")
			USER_ARCH=1
			;;
		--arch=*)
			ARCH=$(normalize_arch "${1#--arch=}")
			USER_ARCH=1
			;;
		-r|--release)
			BUILD_TYPE=Release
			BUILD_TYPE_SUFFIX=release
			USER_BUILD_TYPE_FLAG=1
			;;
		makefiles|Makefiles)
			CMAKE_GENERATOR="Unix Makefiles"
			;;
		menuconfig|Menuconfig)
			RUN_MENUCONFIG=1
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

# rosconfig: build the host configurator, optionally run menuconfig, then
# let the cached selections act as defaults for unspecified options.
if rosconfig_build; then
	ROSCONFIG_OK=1
fi

if [ "$RUN_MENUCONFIG" = "1" ]; then
	[ "$ROSCONFIG_OK" = "1" ] || fail "menuconfig requested but the rosconfig tool could not be built"
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --menu --ask-configure
	menu_status=$?
	if [ "$menu_status" -ne 0 ]; then
		if [ "$menu_status" -eq "$ROSCONFIG_EXIT_SKIP_CONFIGURE" ]; then
			echo "configure.sh: configuration was not started."
			exit 0
		fi
		[ "$menu_status" -eq 130 ] && echo "configure.sh: menuconfig cancelled; build configuration was not started." >&2
		exit "$menu_status"
	fi
fi

if [ "$ROSCONFIG_OK" = "1" ]; then
	# Seed the cache with defaults on first use; merge in newly added options.
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults || ROSCONFIG_OK=0
fi

if [ "$USER_ARCH" = "0" ]; then
	ROSCONFIG_ARCH=$(rosconfig_cache_get ARCH)
	case "$ROSCONFIG_ARCH" in
		"")
			;;
		amd64|i386|arm64|arm)
			ARCH=$ROSCONFIG_ARCH
			;;
		*)
			echo "configure.sh: warning: ignoring unsupported ARCH '$ROSCONFIG_ARCH' from $ROSCONFIG_CACHE" >&2
			;;
	esac
fi
if [ "$USER_TOOLCHAIN" = "0" ]; then
	case "$(rosconfig_cache_get TOOLCHAIN)" in
		gcc)
			USE_CLANG=0
			;;
		clang)
			USE_CLANG=1
			;;
		msvc)
			echo "configure.sh: warning: TOOLCHAIN=msvc from the config cache is only supported by configure.cmd; using clang." >&2
			;;
	esac
fi
if [ "$USER_BUILD_TYPE_FLAG" = "0" ] && [ "$USER_BUILD_TYPE" = "0" ]; then
	case "$(rosconfig_cache_get BUILD_TYPE)" in
		Release)
			BUILD_TYPE=Release
			;;
		Debug)
			BUILD_TYPE=Debug
			;;
	esac
	BUILD_TYPE_SUFFIX=$(lower_build_type "$BUILD_TYPE")
fi

if [ "$USER_BUILD_TYPE" -eq 0 ]; then
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DCMAKE_BUILD_TYPE:STRING=$BUILD_TYPE"
fi

if [ "$USE_CLANG" -eq 1 ]; then
	BUILD_ENVIRONMENT=Clang
	TOOLCHAIN_FILE=toolchain-clang.cmake

	if [ "$ROSBE_SKIP_HOST_CHECK" != "1" ]; then
		[ -x "$ROSBE_LLVM_ROOT/bin/clang" ] || fail "missing RosBE LLVM toolchain at $ROSBE_LLVM_ROOT"
		[ -x "$ROSBE_LLVM_ROOT/bin/clang++" ] || fail "missing RosBE LLVM clang++ at $ROSBE_LLVM_ROOT/bin"
	fi

	export REACTOS_CLANG_LLVM_MINGW_ROOT="$ROSBE_LLVM_ROOT"
	export LLVM_MINGW_ROOT="$ROSBE_LLVM_ROOT"
	export PATH="$ROSBE_LLVM_ROOT/bin:$PATH"

	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DREACTOS_CLANG_LLVM_MINGW_ROOT:PATH=$ROSBE_LLVM_ROOT"
else
	BUILD_ENVIRONMENT=GCC
	TOOLCHAIN_FILE=toolchain-gcc.cmake
	ROSBE_GCC_ROOT="$ROSBE_ROOT/mingw-gcc"
	GCC_TRIPLET=$(gcc_triplet_for_arch "$ARCH")
	GCC_TOOLCHAIN_ROOT="$ROSBE_GCC_ROOT/$GCC_TRIPLET"

	if [ "$ROSBE_SKIP_HOST_CHECK" != "1" ]; then
		[ -x "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-gcc" ] || fail "missing RosBE GCC toolchain for $ARCH at $GCC_TOOLCHAIN_ROOT"
		[ -x "$GCC_TOOLCHAIN_ROOT/bin/$GCC_TRIPLET-g++" ] || fail "missing RosBE GCC g++ for $ARCH at $GCC_TOOLCHAIN_ROOT/bin"
	fi

	export PATH="$GCC_TOOLCHAIN_ROOT/bin:$PATH"
fi

# rosconfig: turn the cached selections into a CMake pre-load fragment that
# /PreLoad.cmake picks up. Explicit -D arguments still take precedence.
ROSCONFIG_CCACHE_ARG="-DENABLE_CCACHE:BOOL=0"
if [ "$ROSCONFIG_OK" = "1" ]; then
	if [ "$USE_CLANG" -eq 1 ]; then
		ROSCONFIG_TOOLCHAIN=clang
	else
		ROSCONFIG_TOOLCHAIN=gcc
	fi
	if "$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" \
		--generate "$ROSCONFIG_OVERRIDES" \
		--override "ARCH=$ARCH" \
		--override "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" \
		--override "BUILD_TYPE=$BUILD_TYPE"; then
		# ENABLE_CCACHE is now controlled by the overrides file.
		ROSCONFIG_CCACHE_ARG=
	else
		rm -f "$ROSCONFIG_OVERRIDES"
	fi
else
	rm -f "$ROSCONFIG_OVERRIDES"
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
if [ "$ROSCONFIG_OK" = "1" ]; then
	echo "Config cache:  $ROSCONFIG_CACHE (change with: ./menuconfig.sh)"
fi
echo

sync_arm64_submodules
prepare_arm64_fex_source

if [ "$REACTOS_SOURCE_DIR" = "$PWD" ]; then
	BUILD_HINT_PATH="./$REACTOS_OUTPUT_PATH"
	echo "Creating directories in $REACTOS_OUTPUT_PATH"
	mkdir -p "$REACTOS_OUTPUT_PATH"
	cd "$REACTOS_OUTPUT_PATH" || exit 1
fi

rm -rf CMakeFiles host-tools/CMakeFiles
rm -f CMakeCache.txt host-tools/CMakeCache.txt

# Do not let host package-manager flags leak into target compiler/linker search
# paths. Target-specific options should be passed through CMake arguments.
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
unset CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH

cmake -G "$CMAKE_GENERATOR" \
	$ROSCONFIG_CCACHE_ARG \
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
