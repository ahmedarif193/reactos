#!/bin/sh

REACTOS_SOURCE_DIR=$(cd `dirname $0` && pwd)
CUSTOM_MINGW_AMD64="$HOME/mingw-toolchains/x86_64-w64-mingw32"
CUSTOM_LLVM_MINGW_AMD64="$HOME/mingw-toolchains/llvm-mingw-20251202-ucrt-ubuntu-22.04-x86_64"
BUILD_ENVIRONMENT=MinGW
TOOLCHAIN_FILE=toolchain-gcc.cmake

usage() {
	echo "Invalid parameter given."
	echo "Usage: configure.sh [options]"
	echo "  clang       - Use Clang/LLVM compiler (default: GCC/MinGW, or local llvm-mingw when RosBE is absent)"
	echo "  makefiles   - Use Unix Makefiles generator (default: Ninja)"
	echo "  -D<var>=<val> - Pass option to CMake"
	exit 1
}

set_build_type_from_cmake_arg() {
	case "$1" in
		-DCMAKE_BUILD_TYPE=*|CMAKE_BUILD_TYPE=*|-DCMAKE_BUILD_TYPE:*=*|CMAKE_BUILD_TYPE:*=*)
			ROS_BUILD_TYPE=${1#*=}
		;;
	esac
}

CMAKE_GENERATOR="Ninja"
while [ $# -gt 0 ]; do
	case $1 in
		-D)
			shift
			if echo "x$1" | grep 'x?*=*' > /dev/null; then
				ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -D $1"
				set_build_type_from_cmake_arg "$1"
			else
				usage
			fi
		;;

		-D?*=*|-D?*)
			ROS_CMAKEOPTS=$ROS_CMAKEOPTS" $1"
			set_build_type_from_cmake_arg "$1"
		;;
		makefiles|Makefiles)
			CMAKE_GENERATOR="Unix Makefiles"
		;;
		clang|Clang)
			BUILD_ENVIRONMENT=Clang
			TOOLCHAIN_FILE=toolchain-clang.cmake
		;;
		*)
			usage
			;;
	esac

	shift
done

if [ "x$ROS_ARCH" = "x" ]; then
	if [ -x "$CUSTOM_LLVM_MINGW_AMD64/bin/x86_64-w64-mingw32-clang" ]; then
		ROS_ARCH=amd64
		BUILD_ENVIRONMENT=Clang
		TOOLCHAIN_FILE=toolchain-clang.cmake
		echo "ROS_ARCH not set; defaulting to amd64 Clang (toolchain: $CUSTOM_LLVM_MINGW_AMD64)"
	elif [ -x "$CUSTOM_MINGW_AMD64/bin/x86_64-w64-mingw32-gcc" ]; then
		ROS_ARCH=amd64
		echo "ROS_ARCH not set; defaulting to amd64 MinGW (toolchain: $CUSTOM_MINGW_AMD64)"
	else
		echo "Could not detect RosBE or a local amd64 toolchain."
		exit 1
	fi
fi

ARCH=$ROS_ARCH
REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH
if [ "$BUILD_ENVIRONMENT" = "Clang" ]; then
	case "$ROS_BUILD_TYPE" in
		Release|release)
			REACTOS_OUTPUT_PATH=$REACTOS_OUTPUT_PATH-release
		;;
	esac
fi

if [ "$BUILD_ENVIRONMENT" = "Clang" ] && [ -x "$CUSTOM_LLVM_MINGW_AMD64/bin/x86_64-w64-mingw32-clang" ]; then
	if [ "x$REACTOS_CLANG_LLVM_MINGW_ROOT" = "x" ]; then
		export REACTOS_CLANG_LLVM_MINGW_ROOT="$CUSTOM_LLVM_MINGW_AMD64"
	fi
	export LLVM_MINGW_ROOT="$REACTOS_CLANG_LLVM_MINGW_ROOT"
	export PATH="$REACTOS_CLANG_LLVM_MINGW_ROOT/bin:$PATH"
	echo "Using llvm-mingw from $REACTOS_CLANG_LLVM_MINGW_ROOT/bin"
elif [ "$ARCH" = "amd64" ] && [ -x "$CUSTOM_MINGW_AMD64/bin/x86_64-w64-mingw32-gcc" ]; then
	export PATH="$CUSTOM_MINGW_AMD64/bin:$PATH"
	echo "Using amd64 MinGW from $CUSTOM_MINGW_AMD64/bin"
fi

echo "Configuring a new ReactOS build on:"
uname -srvpio
echo
echo "RosBE root:    $ROSBE_ROOT"
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

cmake -G "$CMAKE_GENERATOR" -DENABLE_CCACHE:BOOL=0 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=$TOOLCHAIN_FILE -DARCH:STRING=$ARCH $EXTRA_ARGS $ROS_CMAKEOPTS "$REACTOS_SOURCE_DIR"
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
