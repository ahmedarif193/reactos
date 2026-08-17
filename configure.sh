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

usage() {
	echo "Usage: configure.sh [options]"
	echo "  --clang              Use Clang/LLVM from ~/.local/opt/rosbe/llvm-mingw (default)"
	echo "  --gcc                Use GCC from ~/.local/opt/rosbe/mingw-gcc"
	echo "  -a, --arch <arch>    Target architecture: amd64, i386, arm64 (default: amd64)"
	echo "  -r, --release        Configure a Release build (default: Debug)"
	echo "  makefiles            Use Unix Makefiles generator (default: Ninja)"
	echo "  menuconfig           Open the interactive configuration UI first;"
	echo "                       selections persist in the output tree"
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

# KDBG on x86 uses the Zydis and Zycore revisions nested in FEX. Fetch only
# that dependency chain here; recursively fetching FEX would also download its
# large test-binary submodules.
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

sync_kdb_submodules() {
	kdb_zydis_enabled || return 0

	KDB_FEX_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	KDB_ZYDIS_DIR="$KDB_FEX_DIR/External/zydis"
	KDB_ZYCORE_DIR="$KDB_ZYDIS_DIR/dependencies/zycore"
	if [ -f "$KDB_ZYDIS_DIR/src/MetaInfo.c" ] && [ -f "$KDB_ZYCORE_DIR/src/API/Memory.c" ]; then
		return 0
	fi

	command -v git >/dev/null 2>&1 || fail "git is required to initialize the KDBG disassembler submodules"
	git -C "$REACTOS_SOURCE_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "KDBG requires Zydis and Zycore; configure from a recursive Git checkout"

	echo "Syncing KDBG Zydis submodules..."
	git -C "$REACTOS_SOURCE_DIR" submodule sync -- submodules/fex-arm64ec || fail "failed to sync FEX submodule metadata"
	git -C "$REACTOS_SOURCE_DIR" submodule update --init --depth 1 -- submodules/fex-arm64ec || fail "failed to initialize the FEX submodule"
	git -C "$KDB_FEX_DIR" submodule sync -- External/zydis || fail "failed to sync Zydis submodule metadata"
	git -C "$KDB_FEX_DIR" submodule update --init --depth 1 -- External/zydis || fail "failed to initialize the Zydis submodule"
	git -C "$KDB_ZYDIS_DIR" submodule sync -- dependencies/zycore || fail "failed to sync Zycore submodule metadata"
	git -C "$KDB_ZYDIS_DIR" submodule update --init --depth 1 -- dependencies/zycore || fail "failed to initialize the Zycore submodule"
	[ -f "$KDB_ZYDIS_DIR/src/MetaInfo.c" ] && [ -f "$KDB_ZYCORE_DIR/src/API/Memory.c" ] || fail "KDBG Zydis submodules are incomplete after synchronization"
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
	FEX_CHECKOUT_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	if [ -f "$FEX_CHECKOUT_DIR/CMakeLists.txt" ] && git -C "$FEX_CHECKOUT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		FEX_MISSING_SUBMODULES=$(git -C "$FEX_CHECKOUT_DIR" submodule status --recursive 2>/dev/null | sed -n '/^-/p')
		if [ -z "$FEX_MISSING_SUBMODULES" ]; then
			echo "FEX ARM64EC submodules already initialized; skipping sync."
			return 0
		fi
	fi

	echo "Syncing FEX ARM64EC submodule..."
	git -C "$REACTOS_SOURCE_DIR" submodule sync -- submodules/fex-arm64ec || fail "failed to sync FEX submodule metadata"
	git -C "$REACTOS_SOURCE_DIR" submodule update --init --recursive -- submodules/fex-arm64ec || fail "failed to initialize FEX submodule"
}

prepare_arm64_fex_source() {
	[ "$ARCH" = "arm64" ] || return 0

	fex_arm64ec_enabled || return 0

	# The ReactOS changes to FEX live in the fork's main-ros branch (see .gitmodules); the submodule is used as-is.
	FEX_UPSTREAM_DIR="$REACTOS_SOURCE_DIR/submodules/fex-arm64ec"
	FEX_PREPARED_DIR="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH/submodules/fex-arm64ec-src"

	[ -f "$FEX_UPSTREAM_DIR/CMakeLists.txt" ] || fail "FEX submodule source is missing at $FEX_UPSTREAM_DIR"
	[ -f "$FEX_UPSTREAM_DIR/External/fmt/CMakeLists.txt" ] || fail "FEX submodule dependencies are incomplete"
	command -v cksum >/dev/null 2>&1 || fail "cksum is required to identify the prepared FEX source"

	FEX_SOURCE_ID=
	if command -v git >/dev/null 2>&1 && git -C "$FEX_UPSTREAM_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 && git -C "$FEX_UPSTREAM_DIR" diff --quiet --ignore-submodules=dirty HEAD --; then
		FEX_SOURCE_REV=$(git -C "$FEX_UPSTREAM_DIR" rev-parse HEAD) || fail "could not identify the FEX source revision"
		FEX_SUBMODULE_STATE=$(git -C "$FEX_UPSTREAM_DIR" submodule status --recursive) || fail "could not identify FEX submodule revisions"
		FEX_SOURCE_ID=$(printf '%s\n%s\n' "$FEX_SOURCE_REV" "$FEX_SUBMODULE_STATE" | cksum | awk '{print $1 "-" $2}')
	fi
	FEX_PREPARED_STAMP="$FEX_PREPARED_DIR/.reactos-source-id"
	if [ -n "$FEX_SOURCE_ID" ] && [ -f "$FEX_PREPARED_DIR/CMakeLists.txt" ] && [ "$(sed -n '1p' "$FEX_PREPARED_STAMP" 2>/dev/null)" = "$FEX_SOURCE_ID" ]; then
		echo "Prepared FEX ARM64EC source is current; reusing it."
		return 0
	fi

	echo "Preparing FEX ARM64EC source..."
	rm -rf "$FEX_PREPARED_DIR"
	mkdir -p "$(dirname "$FEX_PREPARED_DIR")"
	cp -a "$FEX_UPSTREAM_DIR" "$FEX_PREPARED_DIR"
	rm -rf "$FEX_PREPARED_DIR/.git"
	if [ -n "$FEX_SOURCE_ID" ]; then
		printf '%s\n' "$FEX_SOURCE_ID" > "$FEX_PREPARED_STAMP"
	fi
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

REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH-$BUILD_TYPE_SUFFIX$ROSBE_OUTPUT_SUFFIX
EXPECTED_BUILD_DIR="$REACTOS_SOURCE_DIR/$REACTOS_OUTPUT_PATH"

# Never reconfigure one target's output directory as another target. This check
# runs before any CMake cache or generated build state is removed.
if [ "$REACTOS_START_DIR" = "$REACTOS_SOURCE_DIR" ]; then
	BUILD_DIR="$EXPECTED_BUILD_DIR"
	BUILD_HINT_PATH="./$REACTOS_OUTPUT_PATH"
	echo "Creating directories in $REACTOS_OUTPUT_PATH"
	mkdir -p "$BUILD_DIR"
elif [ "$REACTOS_START_DIR" = "$EXPECTED_BUILD_DIR" ]; then
	BUILD_DIR="$REACTOS_START_DIR"
	BUILD_HINT_PATH="$BUILD_DIR"
else
	fail "refusing to configure '$REACTOS_START_DIR' as $BUILD_ENVIRONMENT/$ARCH/$BUILD_TYPE; expected output directory '$EXPECTED_BUILD_DIR'"
fi

ROSCONFIG_STATE_DIR="$BUILD_DIR/.rosconfig"
ROSCONFIG_CACHE="$ROSCONFIG_STATE_DIR/config.cache"
ROSCONFIG_OVERRIDES="$ROSCONFIG_STATE_DIR/overrides.cmake"
mkdir -p "$ROSCONFIG_STATE_DIR"

if [ "$USE_CLANG" -eq 1 ]; then
	ROSCONFIG_TOOLCHAIN=clang
else
	ROSCONFIG_TOOLCHAIN=gcc
fi

# Build the host configurator and seed this output tree's cache. The target
# identity is persisted rather than existing only as a transient override.
if rosconfig_build; then
	ROSCONFIG_OK=1
	if ! "$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE"; then
		ROSCONFIG_OK=0
	fi
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
	"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults --set "ARCH=$ARCH" --set "TOOLCHAIN=$ROSCONFIG_TOOLCHAIN" --set "BUILD_TYPE=$BUILD_TYPE" || fail "could not preserve the output tree identity in $ROSCONFIG_CACHE"
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

sync_kdb_submodules
sync_arm64_submodules
prepare_arm64_fex_source

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
