#!/bin/sh

# Default to amd64 Debug using the local MinGW toolchain at
# ~/mingw-toolchains/x86_64-w64-mingw32 when RosBE is not loaded.
CUSTOM_MINGW_AMD64="$HOME/mingw-toolchains/x86_64-w64-mingw32"

if [ "x$ROS_ARCH" = "x" ]; then
	if [ -x "$CUSTOM_MINGW_AMD64/bin/x86_64-w64-mingw32-gcc" ]; then
		ROS_ARCH=amd64
		echo "ROS_ARCH not set; defaulting to amd64 (toolchain: $CUSTOM_MINGW_AMD64)"
	else
		echo "Could not detect RosBE and $CUSTOM_MINGW_AMD64 is missing."
		exit 1
	fi
fi

BUILD_ENVIRONMENT=MinGW
ARCH=$ROS_ARCH
REACTOS_SOURCE_DIR=$(cd `dirname $0` && pwd)
REACTOS_OUTPUT_PATH=output-$BUILD_ENVIRONMENT-$ARCH

# For amd64, prefer the bundled x86_64-w64-mingw32 toolchain so CMake's
# find_program picks it up ahead of anything else on PATH.
if [ "$ARCH" = "amd64" ] && [ -x "$CUSTOM_MINGW_AMD64/bin/x86_64-w64-mingw32-gcc" ]; then
	export PATH="$CUSTOM_MINGW_AMD64/bin:$PATH"
	echo "Using amd64 MinGW from $CUSTOM_MINGW_AMD64/bin"
fi

usage() {
	echo "Invalid parameter given."
	exit 1
}

CMAKE_GENERATOR="Ninja"
while [ $# -gt 0 ]; do
	case $1 in
		-D)
			shift
			if echo "x$1" | grep 'x?*=*' > /dev/null; then
				ROS_CMAKEOPTS=$ROS_CMAKEOPTS" -D $1"
			else
				usage
			fi
		;;

		-D?*=*|-D?*)
			ROS_CMAKEOPTS=$ROS_CMAKEOPTS" $1"
		;;
		makefiles|Makefiles)
			CMAKE_GENERATOR="Unix Makefiles"
		;;
		*)
			usage
	esac

	shift
done

echo "Configuring a new ReactOS build on:"
echo $(uname -srvpio); echo

if [ "$REACTOS_SOURCE_DIR" = "$PWD" ]; then
	echo "Creating directories in $REACTOS_OUTPUT_PATH"
	mkdir -p "$REACTOS_OUTPUT_PATH"
	cd "$REACTOS_OUTPUT_PATH"
fi

rm -f CMakeCache.txt host-tools/CMakeCache.txt

cmake -G "$CMAKE_GENERATOR" -DENABLE_CCACHE:BOOL=0 -DCMAKE_BUILD_TYPE:STRING=Debug -DCMAKE_TOOLCHAIN_FILE:FILEPATH=toolchain-gcc.cmake -DARCH:STRING=$ARCH $EXTRA_ARGS $ROS_CMAKEOPTS "$REACTOS_SOURCE_DIR"
if [ $? -ne 0 ]; then
    echo "An error occurred while configuring ReactOS"
    exit 1
fi

echo "Configure script complete! Execute appropriate build commands (e.g. ninja, make, makex, etc.) from $REACTOS_OUTPUT_PATH"
