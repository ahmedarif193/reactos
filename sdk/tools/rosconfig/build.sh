#!/bin/sh

# Incremental host build for rosconfig. Each translation unit is compiled
# independently so UI-only edits do not rebuild the parser and cache code.

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: build.sh <build-directory> <output-binary>" >&2
    exit 2
fi

src_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$1
output=$2
header="$src_dir/rosconfig.h"
compiler_stamp="$build_dir/rosconfig.compiler"

host_cc=${CC:-}
if [ -z "$host_cc" ]; then
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            host_cc=$candidate
            break
        fi
    done
fi
if [ -z "$host_cc" ]; then
    echo "rosconfig: no host C compiler found (install gcc or clang)" >&2
    exit 2
fi

mkdir -p "$build_dir"

compiler_path=$(command -v "$host_cc" 2>/dev/null || printf '%s' "$host_cc")
compiler_version=$("$host_cc" --version 2>/dev/null | sed -n '1p')
compiler_signature="$compiler_path|$compiler_version"
force_rebuild=1
if [ -f "$compiler_stamp" ] && [ "$(sed -n '1p' "$compiler_stamp")" = "$compiler_signature" ]; then
    force_rebuild=0
fi

compile_object()
{
    name=$1
    source="$src_dir/$name.c"
    object="$build_dir/$name.o"
    temporary="$object.tmp"

    if [ "$force_rebuild" -eq 1 ] || [ ! -f "$object" ] || [ "$source" -nt "$object" ] || [ "$header" -nt "$object" ]; then
        echo "  CC      $name.c"
        rm -f "$temporary"
        if ! "$host_cc" -O2 -I"$src_dir" -c "$source" -o "$temporary"; then
            rm -f "$temporary"
            return 1
        fi
        mv -f "$temporary" "$object"
    fi
}

compile_object rosconfig
compile_object rosconfig_util
compile_object rosconfig_model
compile_object rosconfig_ui
compile_object rosconfig_selftest

main_obj="$build_dir/rosconfig.o"
util_obj="$build_dir/rosconfig_util.o"
model_obj="$build_dir/rosconfig_model.o"
ui_obj="$build_dir/rosconfig_ui.o"
selftest_obj="$build_dir/rosconfig_selftest.o"

if [ ! -x "$output" ] || [ "$main_obj" -nt "$output" ] || [ "$util_obj" -nt "$output" ] || [ "$model_obj" -nt "$output" ] || [ "$ui_obj" -nt "$output" ] || [ "$selftest_obj" -nt "$output" ]; then
    temporary="$output.tmp"
    echo "  LINK    $(basename "$output")"
    rm -f "$temporary"
    if ! "$host_cc" -o "$temporary" "$main_obj" "$util_obj" "$model_obj" "$ui_obj" "$selftest_obj"; then
        rm -f "$temporary"
        exit 1
    fi
    mv -f "$temporary" "$output"
fi

printf '%s\n' "$compiler_signature" > "$compiler_stamp"
