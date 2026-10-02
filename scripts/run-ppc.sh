#!/bin/sh
# Boot the NT PowerPC PReP target. Extra arguments are passed to QEMU.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${REACTOS_BUILD_DIR:-"$repo_dir/output-Clang-ppc-debug"}
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
media=disk
do_build=yes
disk_image=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --img) media=disk; disk_image=; shift ;;
        --livecd) media=iso; disk_image=; shift ;;
        --disk-image)
            if [ "$#" -lt 2 ] || [ ! -f "$2" ]; then
                echo "--disk-image requires an existing disk image." >&2
                exit 2
            fi
            disk_image=$(realpath -- "$2")
            media=disk
            do_build=no
            shift 2 ;;
        --no-build) do_build=no; shift ;;
        --) shift; break ;;
        -h|--help)
            echo "Usage: $0 [--img|--livecd|--disk-image PATH] [--no-build] [-- QEMU arguments]"
            echo "--disk-image boots an existing disk without rebuilding it."
            echo "Environment: REACTOS_BUILD_DIR, ROS_QEMU_PPC, ROS_QEMU_DISPLAY (default gtk)"
            exit 0 ;;
        *) echo "Unknown option: $1 (use -- before QEMU arguments)" >&2; exit 2 ;;
    esac
done

if ! grep -q '^ARCH:STRING=ppc$' "$build_dir/CMakeCache.txt"; then
    echo "Expected a configured PowerPC build: $build_dir" >&2
    exit 1
fi

qemu_binary=${ROS_QEMU_PPC:-$(command -v qemu-system-ppc || true)}
if [ -z "$qemu_binary" ]; then
    user_dir=$(python3 -c 'import os,pwd; print(pwd.getpwuid(os.getuid()).pw_dir)')
    qemu_binary="$user_dir/.local/opt/qemu-ppc/root/usr/bin/qemu-system-ppc"
fi
if [ ! -x "$qemu_binary" ]; then
    echo "Set ROS_QEMU_PPC to qemu-system-ppc." >&2
    exit 1
fi

# A relocated QEMU must load modules from the same installation.
if [ -z "${QEMU_MODULE_DIR:-}" ]; then
    for module_dir in "$(dirname -- "$qemu_binary")"/../lib/*/qemu; do
        if [ -d "$module_dir" ]; then
            QEMU_MODULE_DIR=$module_dir
            export QEMU_MODULE_DIR
            break
        fi
    done
fi

target=reactosimg
image=${disk_image:-"$build_dir/ReactOS.img"}
if [ "$media" = iso ]; then
    target=livecd
    image="$build_dir/livecd.iso"
fi
if [ "$do_build" = yes ]; then
    ninja -C "$build_dir" -j "${ROS_BUILD_JOBS:-8}" "$target"
fi

if [ "$media" = iso ]; then
    set -- -cdrom "$image" -drive "if=none,id=ppc_os,format=raw,media=cdrom,file=$image" \
        -device ide-cd,drive=ppc_os,bus=ahci.0 -boot d "$@"
else
    # OpenBIOS reads SCSI before handoff; ReactOS accesses the AHCI view.
    image_format=$(python3 -c 'import sys; print("qcow2" if open(sys.argv[1], "rb").read(4) == b"QFI\xfb" else "raw")' "$image")
    set -- -drive "if=scsi,index=0,readonly=on,format=$image_format,file.locking=off,file=$image" \
        -drive "if=none,id=ppc_os,format=$image_format,file=$image" \
        -device ide-hd,drive=ppc_os,bus=ahci.0 -boot c "$@"
fi

echo "PowerPC 40p / 604, 192 MiB. Serial log: $build_dir/ppc-serial.log"
exec "$qemu_binary" -L "${ROS_QEMU_PPC_FIRMWARE_DIR:-/usr/share/qemu}" \
    -machine 40p -cpu 604 -m 192 \
    -kernel "$build_dir/boot/freeldr/freeldr/ppcboot.bin" \
    -device ich9-ahci,id=ahci \
    -nographic -display "${ROS_QEMU_DISPLAY:-gtk}" \
    -serial "file:$build_dir/ppc-serial.log" -monitor none -no-reboot "$@"
