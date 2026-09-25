#!/bin/sh

REACTOS_SOURCE_DIR=$(cd "$(dirname "$0")" && pwd -P)
REACTOS_START_DIR=$(pwd -P)

# rosconfig (menuconfig) support: the host tool is built below the source tree,
# while every output tree owns its configuration cache and CMake pre-load file.
# See sdk/tools/rosconfig/README.md.
ROSCONFIG_DIR="$REACTOS_SOURCE_DIR/.rosconfig"
ROSCONFIG_BIN="$ROSCONFIG_DIR/rosconfig"
ROSCONFIG_BUILD="$REACTOS_SOURCE_DIR/sdk/tools/rosconfig/build.sh"
ROSCONFIG_DEF="$REACTOS_SOURCE_DIR/sdk/cmake/rosconfig.def"
ROSCONFIG_STATE_DIR=
ROSCONFIG_CACHE=
ROSCONFIG_OVERRIDES=
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
SKIP_FEEDS_UPDATE=0

usage() {
	echo "Usage: configure.sh [options]"
	echo "  --clang              Use Clang/LLVM from ~/.local/opt/rosbe/llvm-mingw (default)"
	echo "  --gcc                Use GCC from ~/.local/opt/rosbe/mingw-gcc"
	echo "  -a, --arch <arch>    Target architecture: amd64, i386, arm64 (default: amd64)"
	echo "  -r, --release        Configure a Release build (default: Debug)"
	echo "  makefiles            Use Unix Makefiles generator (default: Ninja)"
	echo "  menuconfig           Open the interactive configuration UI first;"
	echo "                       selections persist in the output tree"
	echo "  --no-feeds-update    Do not fetch the source feeds declared in feeds.conf"
	echo "  -D<var>=<val>        Pass option to CMake"
	exit 1
}

fail() {
	echo "configure.sh: $*" >&2
	exit 1
}

# Read one raw value from a rosconfig cache (empty output if absent).
rosconfig_file_get() {
	[ -f "$1" ] || return 0
	sed -n "s/^$2=//p" "$1" | head -n 1
}

rosconfig_cache_get() {
	rosconfig_file_get "$ROSCONFIG_CACHE" "$1"
}

# Read one value from an existing CMake cache.
cmake_cache_get() {
	[ -f "$1" ] || return 0
	sed -n "s/^$2:[^=]*=//p" "$1" | head -n 1
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

# FEX ARM64EC is enabled by default for ARM64 builds. An explicit CMake value
# takes precedence over menuconfig; otherwise the rosconfig value is used.
fex_arm64ec_enabled() {
	FEX_ARM64EC_OVERRIDE_SET=0
	FEX_ARM64EC_OVERRIDE=
	for FEX_ARM64EC_ARG in $ROS_CMAKEOPTS; do
		case "$FEX_ARM64EC_ARG" in
			-DENABLE_FEX_ARM64EC=*|-DENABLE_FEX_ARM64EC:*=*)
				FEX_ARM64EC_OVERRIDE_SET=1
				FEX_ARM64EC_OVERRIDE=${FEX_ARM64EC_ARG#*=}
				;;
		esac
	done

	if [ "$FEX_ARM64EC_OVERRIDE_SET" = "1" ]; then
		FEX_ARM64EC_OVERRIDE=$(printf '%s' "$FEX_ARM64EC_OVERRIDE" | tr '[:lower:]' '[:upper:]')
		case "$FEX_ARM64EC_OVERRIDE" in
			""|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|*-NOTFOUND)
				return 1
				;;
			*)
				return 0
				;;
		esac
	fi

	case "$(rosconfig_cache_get ENABLE_FEX_ARM64EC)" in
		y) return 0 ;;
		n) return 1 ;;
	esac

	return 0
}

optional_fex_warning() {
	echo "configure.sh: warning: $*. FEX ARM64EC is optional; configuration will continue." >&2
}

sync_glmark2_submodule() {
	GLMARK2_DIR="$REACTOS_SOURCE_DIR/base/applications/cmdutils/glmark2"
	if [ -f "$GLMARK2_DIR/src/benchmark-collection.cpp" ] &&
	   [ -f "$GLMARK2_DIR/src/zlib/adler32.c" ]; then
		return 0
	fi

	command -v git >/dev/null 2>&1 || fail "git is required to initialize the glmark2 submodule"
	git -C "$REACTOS_SOURCE_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "glmark2 sources are missing; configure from a recursive Git checkout"

	echo "Syncing glmark2 submodule..."
	git -C "$REACTOS_SOURCE_DIR" submodule sync -- base/applications/cmdutils/glmark2 || fail "failed to sync glmark2 submodule metadata"
	git -C "$REACTOS_SOURCE_DIR" submodule update --init --depth 1 -- base/applications/cmdutils/glmark2 || fail "failed to initialize the glmark2 submodule"
	[ -f "$GLMARK2_DIR/src/benchmark-collection.cpp" ] &&
	[ -f "$GLMARK2_DIR/src/zlib/adler32.c" ] || fail "glmark2 submodule is incomplete after synchronization"
}

# External sources live in their own repositories, declared in feeds.conf.
# They are checked out under submodules/ and track their configured branch.
sync_feeds() {
	[ "$SKIP_FEEDS_UPDATE" = "0" ] || return 0
	[ -x "$REACTOS_SOURCE_DIR/scripts/feeds" ] || fail "missing $REACTOS_SOURCE_DIR/scripts/feeds"

	FEEDS_WANTED=mesa
	if [ "$ARCH" = "arm64" ] && fex_arm64ec_enabled; then
		FEEDS_WANTED="$FEEDS_WANTED fex-arm64ec"
	elif kdb_zydis_enabled; then
		# KDBG takes its disassembler from the FEX feed even without ARM64EC.
		FEEDS_WANTED="$FEEDS_WANTED fex-arm64ec"
	fi

	echo "Updating source feeds:$FEEDS_WANTED"
	# shellcheck disable=SC2086
	"$REACTOS_SOURCE_DIR/scripts/feeds" update $FEEDS_WANTED ||
		fail "could not update the source feeds; pass --no-feeds-update to skip"
}

# KDBG on x86 uses the Zydis and Zycore revisions vendored with FEX. Verify
# those sources before CMake consumes them.
kdb_zydis_enabled() {
	KDB_CMAKEOPTS_UPPER=$(printf '%s' "$ROS_CMAKEOPTS" | tr '[:lower:]' '[:upper:]')
	case "$ARCH" in
		i386|amd64)
			;;
		*)
			return 1
			;;
	esac

	case " $KDB_CMAKEOPTS_UPPER " in
		*" -DKD_DEBUGGER=NONE "*|*" -DKD_DEBUGGER:STRING=NONE "*|*" -DKD_DEBUGGER=EXTERNAL "*|*" -DKD_DEBUGGER:STRING=EXTERNAL "*|*" -DKDBG=OFF "*|*" -DKDBG:BOOL=OFF "*|*" -DKDBG=FALSE "*|*" -DKDBG:BOOL=FALSE "*|*" -DKDBG=0 "*|*" -DKDBG:BOOL=0 "*)
			return 1
			;;
		*" -DKD_DEBUGGER=KDBG "*|*" -DKD_DEBUGGER:STRING=KDBG "*|*" -DKDBG=ON "*|*" -DKDBG:BOOL=ON "*|*" -DKDBG=TRUE "*|*" -DKDBG:BOOL=TRUE "*|*" -DKDBG=1 "*|*" -DKDBG:BOOL=1 "*)
			return 0
			;;
	esac

	case "$(rosconfig_cache_get KD_DEBUGGER)" in
		NONE|none|EXTERNAL|external)
			return 1
			;;
		KDBG|kdbg)
			return 0
			;;
	esac

	[ "$BUILD_TYPE" = "Debug" ] || [ "$(rosconfig_cache_get DBG)" = "y" ]
}

verify_kdb_sources() {
	kdb_zydis_enabled || return 0

	KDB_FEX_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	KDB_ZYDIS_DIR="$KDB_FEX_DIR/External/zydis"
	KDB_ZYCORE_DIR="$KDB_ZYDIS_DIR/dependencies/zycore"
	[ -f "$KDB_ZYDIS_DIR/src/MetaInfo.c" ] && [ -f "$KDB_ZYCORE_DIR/src/API/Memory.c" ] ||
		fail "KDBG requires the Zydis and Zycore sources from the fex-arm64ec feed; run scripts/feeds update fex-arm64ec"
}

verify_arm64_sources() {
	[ "$ARCH" = "arm64" ] || return 0

	if ! fex_arm64ec_enabled; then
		echo "FEX ARM64EC disabled by configuration; skipping its source check."
		return 0
	fi

	FEX_CHECKOUT_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	[ -f "$FEX_CHECKOUT_DIR/CMakeLists.txt" ] &&
		[ -f "$FEX_CHECKOUT_DIR/External/fmt/CMakeLists.txt" ] &&
		[ -f "$FEX_CHECKOUT_DIR/External/range-v3/CMakeLists.txt" ] &&
		[ -f "$FEX_CHECKOUT_DIR/External/rpmalloc/CMakeLists.txt" ] &&
		[ -f "$FEX_CHECKOUT_DIR/External/unordered_dense/CMakeLists.txt" ] &&
		[ -f "$FEX_CHECKOUT_DIR/External/xxhash/cmake_unofficial/CMakeLists.txt" ] ||
		optional_fex_warning "the fex-arm64ec feed is incomplete; run scripts/feeds update fex-arm64ec"
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
		--no-feeds-update)
			SKIP_FEEDS_UPDATE=1
			shift
			continue
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

# When configure.sh is launched from an existing output tree, use that tree's
# identity as the default. Command-line target flags still win. No source-wide
# configuration cache participates in target selection.
if [ "$REACTOS_START_DIR" != "$REACTOS_SOURCE_DIR" ]; then
	CURRENT_CMAKE_CACHE="$REACTOS_START_DIR/CMakeCache.txt"
	CURRENT_ROSCONFIG_CACHE="$REACTOS_START_DIR/.rosconfig/config.cache"
	if [ "$USER_ARCH" = "0" ]; then
		CACHED_ARCH=$(cmake_cache_get "$CURRENT_CMAKE_CACHE" ARCH)
		[ -n "$CACHED_ARCH" ] || CACHED_ARCH=$(rosconfig_file_get "$CURRENT_ROSCONFIG_CACHE" ARCH)
		case "$CACHED_ARCH" in
			amd64|i386|arm64|arm) ARCH=$CACHED_ARCH ;;
		esac
	fi
	if [ "$USER_TOOLCHAIN" = "0" ]; then
		CACHED_TOOLCHAIN_FILE=$(cmake_cache_get "$CURRENT_CMAKE_CACHE" CMAKE_TOOLCHAIN_FILE)
		case "$CACHED_TOOLCHAIN_FILE" in
			*toolchain-clang.cmake) USE_CLANG=1 ;;
			*toolchain-gcc.cmake) USE_CLANG=0 ;;
			*)
				case "$(rosconfig_file_get "$CURRENT_ROSCONFIG_CACHE" TOOLCHAIN)" in
					clang) USE_CLANG=1 ;;
					gcc) USE_CLANG=0 ;;
				esac
				;;
		esac
	fi
	if [ "$USER_BUILD_TYPE_FLAG" = "0" ] && [ "$USER_BUILD_TYPE" = "0" ]; then
		CACHED_BUILD_TYPE=$(cmake_cache_get "$CURRENT_CMAKE_CACHE" CMAKE_BUILD_TYPE)
		[ -n "$CACHED_BUILD_TYPE" ] || CACHED_BUILD_TYPE=$(rosconfig_file_get "$CURRENT_ROSCONFIG_CACHE" BUILD_TYPE)
		case "$CACHED_BUILD_TYPE" in
			Debug|Release) BUILD_TYPE=$CACHED_BUILD_TYPE ;;
		esac
		BUILD_TYPE_SUFFIX=$(lower_build_type "$BUILD_TYPE")
	fi
fi

if [ "$USE_CLANG" -eq 1 ]; then
	BUILD_ENVIRONMENT=Clang
else
	BUILD_ENVIRONMENT=GCC
fi

REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH-$BUILD_TYPE_SUFFIX$ROSBE_OUTPUT_SUFFIX
EXPECTED_BUILD_DIR="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH"

# Never reconfigure one target's output directory as another target. This check
# runs before any CMake cache or generated build state is removed.
if [ "$REACTOS_START_DIR" = "$REACTOS_SOURCE_DIR" ]; then
	BUILD_DIR="$EXPECTED_BUILD_DIR"
	BUILD_HINT_PATH="./$REACTOS_OUTPUT_PATH"
	if [ "$RUN_MENUCONFIG" = "0" ]; then
		echo "Creating directories in $REACTOS_OUTPUT_PATH"
		mkdir -p "$BUILD_DIR"
	fi
elif [ "$REACTOS_START_DIR" = "$EXPECTED_BUILD_DIR" ]; then
	BUILD_DIR="$REACTOS_START_DIR"
	BUILD_HINT_PATH="$BUILD_DIR"
else
	fail "refusing to configure '$REACTOS_START_DIR' as $BUILD_ENVIRONMENT/$ARCH/$BUILD_TYPE; expected output directory '$EXPECTED_BUILD_DIR'"
fi

ROSCONFIG_STATE_DIR="$BUILD_DIR/.rosconfig"
ROSCONFIG_CACHE="$ROSCONFIG_STATE_DIR/config.cache"
ROSCONFIG_OVERRIDES="$ROSCONFIG_STATE_DIR/overrides.cmake"
if [ "$RUN_MENUCONFIG" = "0" ]; then
	mkdir -p "$ROSCONFIG_STATE_DIR"
fi

if [ "$USE_CLANG" -eq 1 ]; then
	ROSCONFIG_TOOLCHAIN=clang
else
	ROSCONFIG_TOOLCHAIN=gcc
fi

# Build the host configurator and seed this output tree's cache. The target
# identity is persisted rather than existing only as a transient override.
if rosconfig_build; then
	ROSCONFIG_OK=1
	if [ "$RUN_MENUCONFIG" = "0" ]; then
		if ! "$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE"; then
			ROSCONFIG_OK=0
		fi
	fi
fi

if [ "$RUN_MENUCONFIG" = "1" ]; then
	[ "$ROSCONFIG_OK" = "1" ] || fail "menuconfig requested but the rosconfig tool could not be built"
	ROSCONFIG_MENU_DIR=$(mktemp -d "${TMPDIR:-/tmp}/reactos-menuconfig.XXXXXX") || fail "could not create a temporary menuconfig directory"
	ROSCONFIG_MENU_CACHE="$ROSCONFIG_MENU_DIR/config.cache"
	trap 'rm -rf "$ROSCONFIG_MENU_DIR"' 0 1 2 3 15
	if [ -f "$ROSCONFIG_CACHE" ]; then
		cp "$ROSCONFIG_CACHE" "$ROSCONFIG_MENU_CACHE" || fail "could not prepare the menuconfig cache"
	fi
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_MENU_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE" || fail "could not initialize the menuconfig cache"
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_MENU_CACHE" --menu --ask-configure
	menu_status=$?
	if [ "$menu_status" -ne 0 ] && [ "$menu_status" -ne "$ROSCONFIG_EXIT_SKIP_CONFIGURE" ]; then
		[ "$menu_status" -eq 130 ] && echo "configure.sh: menuconfig cancelled; build configuration was not started." >&2
		exit "$menu_status"
	fi

	# Unlike standalone menuconfig, the integrated workflow may select another
	# target. Command-line target options still take precedence over the menu.
	if [ "$USER_ARCH" = "0" ]; then
		ARCH=$(rosconfig_file_get "$ROSCONFIG_MENU_CACHE" ARCH)
	fi
	case "$ARCH" in
		amd64|i386|arm64|arm) ;;
		*) fail "menuconfig selected an unsupported architecture: $ARCH" ;;
	esac

	if [ "$USER_TOOLCHAIN" = "0" ]; then
		case "$(rosconfig_file_get "$ROSCONFIG_MENU_CACHE" TOOLCHAIN)" in
			clang) USE_CLANG=1 ;;
			gcc) USE_CLANG=0 ;;
			msvc) fail "the MSVC toolchain selected by menuconfig is only supported by configure.cmd" ;;
			*) fail "menuconfig selected an unsupported toolchain" ;;
		esac
	fi

	if [ "$USER_BUILD_TYPE_FLAG" = "0" ] && [ "$USER_BUILD_TYPE" = "0" ]; then
		BUILD_TYPE=$(rosconfig_file_get "$ROSCONFIG_MENU_CACHE" BUILD_TYPE)
		case "$BUILD_TYPE" in
			Debug|Release) ;;
			*) fail "menuconfig selected an unsupported build type: $BUILD_TYPE" ;;
		esac
		BUILD_TYPE_SUFFIX=$(lower_build_type "$BUILD_TYPE")
	fi

	if [ "$USE_CLANG" -eq 1 ]; then
		BUILD_ENVIRONMENT=Clang
		ROSCONFIG_TOOLCHAIN=clang
	else
		BUILD_ENVIRONMENT=GCC
		ROSCONFIG_TOOLCHAIN=gcc
	fi
	REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH-$BUILD_TYPE_SUFFIX$ROSBE_OUTPUT_SUFFIX
	BUILD_DIR="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH"
	BUILD_HINT_PATH="$BUILD_DIR"
	ROSCONFIG_STATE_DIR="$BUILD_DIR/.rosconfig"
	ROSCONFIG_CACHE="$ROSCONFIG_STATE_DIR/config.cache"
	ROSCONFIG_OVERRIDES="$ROSCONFIG_STATE_DIR/overrides.cmake"
	mkdir -p "$ROSCONFIG_STATE_DIR" || fail "could not create $ROSCONFIG_STATE_DIR"
	cp "$ROSCONFIG_MENU_CACHE" "$ROSCONFIG_CACHE" || fail "could not store the menuconfig selections in $ROSCONFIG_CACHE"
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE" || fail "could not preserve the output tree identity in $ROSCONFIG_CACHE"
	rm -rf "$ROSCONFIG_MENU_DIR"
	trap - 0 1 2 3 15

	if [ "$menu_status" -eq "$ROSCONFIG_EXIT_SKIP_CONFIGURE" ]; then
		echo "configure.sh: configuration stored in $ROSCONFIG_CACHE; CMake was not started."
		exit 0
	fi
fi

# Add the implicit build type only after menuconfig has had a chance to change
# it. An explicit -DCMAKE_BUILD_TYPE argument was already retained above.
if [ "$USER_BUILD_TYPE" -eq 0 ]; then
	ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -DCMAKE_BUILD_TYPE:STRING=$BUILD_TYPE"
fi

# Resolve and validate the toolchain only after menuconfig has made its final
# target selection.
if [ "$USE_CLANG" -eq 1 ]; then
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

# Turn this output tree's selections into the CMake pre-load fragment consumed
# by /PreLoad.cmake. Explicit -D arguments still take precedence.
ROSCONFIG_CCACHE_ARG="-DENABLE_CCACHE:BOOL=0"
if [ "$ROSCONFIG_OK" = "1" ]; then
	if "$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --generate "$ROSCONFIG_OVERRIDES"; then
		ROSCONFIG_CCACHE_ARG=
	else
		rm -f "$ROSCONFIG_OVERRIDES"
	fi
else
	rm -f "$ROSCONFIG_OVERRIDES"
fi

echo "Configuring a new ReactOS build on:"
uname -srm
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
	echo "Config cache:  $ROSCONFIG_CACHE"
fi
echo

sync_glmark2_submodule
sync_feeds
verify_kdb_sources
verify_arm64_sources

cd "$BUILD_DIR" || exit 1

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
