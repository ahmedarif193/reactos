#!/usr/bin/env bash

# add_usb_rw_partition.sh
#
# Flash a ReactOS ISO onto a block device and carve out a small writable
# FAT partition for runtime logs/configs. The ISO build remains untouched
# (fast), and the writable area lives in the new partition at the end of
# the device.
#
# Example (requires root privileges):
#   sudo boot/tools/add_usb_rw_partition.sh \
#       --iso output-MinGW-amd64-Debug/livecd.iso \
#       --device /dev/sdX
#
# To keep the ISO build unchanged you can also skip the flash step and only
# create/format the writable partition:
#   sudo boot/tools/add_usb_rw_partition.sh --device /dev/sdX --skip-iso

set -euo pipefail

RW_SIZE_MB=64
LABEL="REACTOS_RW"
ISO_PATH=""
DEVICE=""
WRITE_ISO=1
FORCE=0

help() {
    sed -n '1,120p' "$0"
    exit 0
}

err() {
    echo "[usb-rw] Error: $*" >&2
    exit 1
}

warn() {
    echo "[usb-rw] Warning: $*" >&2
}

info() {
    echo "[usb-rw] $*"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || err "Missing required command: $1"
}

partition_path() {
    local dev="$1"
    local part="$2"
    if [[ "$dev" =~ [0-9]$ ]]; then
        echo "${dev}p${part}"
    else
        echo "${dev}${part}"
    fi
}

last_partition_id() {
    local dev="$1"
    sgdisk --print "$dev" | awk '/^[[:space:]]*[0-9]+/ {last=$1} END {if (last=="") last=0; print last}'
}

partition_id_for_label() {
    local dev="$1" label="$2"
    sgdisk --print "$dev" | awk -v label="$label" '/^[[:space:]]*[0-9]+/ {num=$1; name=""; for (i=7;i<=NF;++i) {if (i==7) name=$i; else name=name" "$i;} if (name==label) {print num; exit}}'
}

confirm_or_exit() {
    local prompt="$1"
    if (( FORCE )); then
        return
    fi
    read -r -p "$prompt [y/N] " reply || exit 1
    case "$reply" in
        [Yy][Ee][Ss]|[Yy]) ;;
        *) err "Aborted by user" ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iso)
            ISO_PATH="$2"; shift 2;;
        --device)
            DEVICE="$2"; shift 2;;
        --rw-size)
            RW_SIZE_MB="$2"; shift 2;;
        --label)
            LABEL="$2"; shift 2;;
        --skip-iso)
            WRITE_ISO=0; shift 1;;
        --force)
            FORCE=1; shift 1;;
        -h|--help)
            help;;
        *)
            err "Unknown argument: $1";;
    esac
done

[[ -n "$DEVICE" ]] || err "Missing --device"
[[ -b "$DEVICE" ]] || err "Device not found or not a block device: $DEVICE"

if (( WRITE_ISO )); then
    [[ -n "$ISO_PATH" ]] || err "Missing --iso"
    [[ -f "$ISO_PATH" ]] || err "ISO not found: $ISO_PATH"
fi

[[ "$RW_SIZE_MB" =~ ^[0-9]+$ ]] || err "--rw-size must be an integer"
(( RW_SIZE_MB >= 16 )) || warn "Writable size is quite small (${RW_SIZE_MB} MiB)"

require_cmd sgdisk
require_cmd mkfs.fat
require_cmd partprobe
(( WRITE_ISO )) && require_cmd dd

if [[ $(id -u) -ne 0 ]]; then
    warn "This script typically needs root privileges to access block devices"
fi

LABEL=${LABEL^^}

confirm_or_exit "About to modify $DEVICE. ALL DATA MAY BE LOST. Continue?"

if (( WRITE_ISO )); then
    info "Writing ISO to $DEVICE"
    dd if="$ISO_PATH" of="$DEVICE" bs=4M conv=fsync status=progress
    sync
fi

info "Creating ${RW_SIZE_MB} MiB writable partition"

local_label="${LABEL}"
before_last=$(last_partition_id "$DEVICE")

sgdisk --new=0:-${RW_SIZE_MB}M:0 --typecode=0:0700 --change-name=0:"${local_label}" "$DEVICE" >/dev/null

partprobe "$DEVICE" || true
command -v udevadm >/dev/null 2>&1 && udevadm settle || true

new_part=$(partition_id_for_label "$DEVICE" "$local_label")
if [[ -z "$new_part" ]]; then
    after_last=$(last_partition_id "$DEVICE")
    if [[ "$after_last" == "$before_last" ]]; then
        err "Failed to determine new partition number"
    fi
    new_part="$after_last"
fi

part_path=$(partition_path "$DEVICE" "$new_part")
[[ -b "$part_path" ]] || err "New partition device not found: $part_path"

info "Formatting $part_path as FAT (label=${local_label})"
mkfs.fat -F 32 -n "$local_label" "$part_path" >/dev/null

info "Writable partition ready: $part_path"
info "ISO content remains on the original partition(s); USB is ready for RW usage"

