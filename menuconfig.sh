#!/bin/sh
# menuconfig.sh - interactive ReactOS build configuration (OpenWrt/kconfig
# style). Compiles the rosconfig host tool on first use, opens the terminal
# UI to enable/disable build options, and stores the selections in the
# untracked .rosconfig/config.cache. The generated overrides.cmake is picked
# up by /PreLoad.cmake whenever a tree is configured (configure.sh,
# configure.cmd or plain cmake). See sdk/tools/rosconfig/README.md.

REACTOS_SOURCE_DIR=$(cd "$(dirname "$0")" && pwd)

ROSCONFIG_DIR="$REACTOS_SOURCE_DIR/.rosconfig"
ROSCONFIG_BIN="$ROSCONFIG_DIR/rosconfig"
ROSCONFIG_BUILD="$REACTOS_SOURCE_DIR/sdk/tools/rosconfig/build.sh"
ROSCONFIG_DEF="$REACTOS_SOURCE_DIR/sdk/cmake/rosconfig.def"
ROSCONFIG_CACHE="$ROSCONFIG_DIR/config.cache"
ROSCONFIG_OVERRIDES="$ROSCONFIG_DIR/overrides.cmake"

fail() {
	echo "menuconfig.sh: $*" >&2
	exit 1
}

[ -x "$ROSCONFIG_BUILD" ] || fail "missing $ROSCONFIG_BUILD"
"$ROSCONFIG_BUILD" "$ROSCONFIG_DIR" "$ROSCONFIG_BIN" || fail "failed to build the rosconfig tool"

if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
	exec "$ROSCONFIG_BIN" --self-test
fi
[ "$#" -eq 0 ] || fail "usage: ./menuconfig.sh [--self-test]"

"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --menu
menu_status=$?
if [ "$menu_status" -ne 0 ]; then
	[ "$menu_status" -eq 130 ] && echo "menuconfig.sh: cancelled; configuration was not regenerated." >&2
	exit "$menu_status"
fi

# Make sure the cache exists even if the user quit without saving, and
# refresh the CMake fragment consumed by /PreLoad.cmake.
"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --defaults || fail "could not write $ROSCONFIG_CACHE"
"$ROSCONFIG_BIN" --def "$ROSCONFIG_DEF" --cache "$ROSCONFIG_CACHE" --generate "$ROSCONFIG_OVERRIDES" || fail "could not write $ROSCONFIG_OVERRIDES"

echo ""
echo "Configuration stored in .rosconfig/config.cache."
echo "Run ./configure.sh to configure a build tree with these settings"
echo "(command-line flags and -D options still take precedence)."
