#!/usr/bin/env bash

set -euo pipefail

linker=
arguments=()

while (($# != 0)); do
    case "$1" in
        --riscv-native-lld=*)
            linker=${1#*=}
            ;;
        -m)
            if (($# < 2)); then
                echo "riscv64-native-link: missing emulation after -m" >&2
                exit 2
            fi
            if [[ $2 != riscv64pe && $2 != elf64lriscv ]]; then
                echo "riscv64-native-link: unexpected linker emulation: $2" >&2
                exit 2
            fi
            arguments+=(-m elf64lriscv)
            shift
            ;;
        -Bdynamic|--enable-auto-image-base|--disable-auto-import|--disable-stdcall-fixup|--gc-sections)
            # These defaults belong to the ordinary PE link and must not reach
            # the address-preserving ELF intermediate.
            ;;
        -L*)
            # Native images use only explicitly named ReactOS archives.
            ;;
        *)
            arguments+=("$1")
            ;;
    esac
    shift
done

if [[ -z $linker || ! -x $linker ]]; then
    echo "riscv64-native-link: version-matched ld.lld is unavailable" >&2
    exit 2
fi

exec "$linker" "${arguments[@]}"
