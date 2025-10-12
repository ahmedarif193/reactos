#!/usr/bin/env bash

# make_reactos_img.sh
# Build helpers for "liveimg" in two modes:
#  1) iso  : copy/hybridize livecd.iso into an IMG (content-identical, RO semantics)
#  2) fat32: create a real RW FAT32 disk image using the same file list as LiveCD
#
# Usage (iso mode):
#   make_reactos_img.sh --mode iso --input livecd.iso --output liveimg.img [--mbr isombr.bin] [--type 0x96] [--uefi]
#
# Usage (fat32 mode):
#   make_reactos_img.sh --mode fat32 --output liveimg.img --size <MiB> \
#       --build-root <build-dir> --list <livecd.<cfg>.lst> [--uefi-only] [--viostor <path>]
#
# Notes:
# - fat32 mode installs ReactOS MBR and FAT boot sectors and mirrors the LiveCD
#   file set into the FAT partition. It also places a copy of FREELDR.SYS at
#   the root for BIOS boot, while keeping loader/freeldr.sys from the LiveCD.

set -euo pipefail

err() { echo "Error: $*" >&2; exit 1; }
info() { echo "[liveimg] $*"; }

MODE="iso"
IN_ISO=""
OUT_IMG=""
MBR_FILE=""
PART_TYPE=""
UEFI=0

# fat32 mode vars
SIZE_MIB=600
BUILD_ROOT=""
LIST_FILE=""
UEFI_ONLY=0
FATTEN_BIN=""
VIOSTOR_SRC=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="$2"; shift 2;;
    -i|--input)
      IN_ISO="$2"; shift 2;;
    -o|--output|--out)
      OUT_IMG="$2"; shift 2;;
    -b|--mbr)
      MBR_FILE="$2"; shift 2;;
    -t|--type)
      PART_TYPE="$2"; shift 2;;
    --uefi)
      UEFI=1; shift 1;;
    --size)
      SIZE_MIB="$2"; shift 2;;
    --build-root)
      BUILD_ROOT="$2"; shift 2;;
    --list)
      LIST_FILE="$2"; shift 2;;
    --uefi-only)
      UEFI_ONLY=1; shift 1;;
    --viostor)
      VIOSTOR_SRC="$2"; shift 2;;
    -h|--help)
      sed -n '1,120p' "$0"; exit 0;;
    *)
      err "Unknown argument: $1";;
  esac
done

[[ -n "$OUT_IMG" ]] || err "Missing --output"

mkdir -p "$(dirname "$OUT_IMG")"

if [[ "$MODE" == "iso" ]]; then
  [[ -n "$IN_ISO" ]] || err "Missing --input"
  [[ -f "$IN_ISO" ]] || err "Input ISO not found: $IN_ISO"

  info "Copying ISO to IMG: $IN_ISO -> $OUT_IMG"
  cp -f "$IN_ISO" "$OUT_IMG"

  # Prefer the build toolchain wrapper if available
  HYB_TOOL=""
  if command -v native-isohybrid >/dev/null 2>&1; then
    HYB_TOOL="native-isohybrid"
  elif command -v isohybrid >/dev/null 2>&1; then
    HYB_TOOL="isohybrid"
  fi

  if [[ -n "$HYB_TOOL" ]]; then
    ARGS=()
    [[ -n "$MBR_FILE" ]] && ARGS+=( -b "$MBR_FILE" )
    [[ -n "$PART_TYPE" ]] && ARGS+=( -t "$PART_TYPE" )
    if (( UEFI == 1 )); then
      # Use short option for broader compatibility
      ARGS+=( -u )
    fi
    info "Hybridizing via: $HYB_TOOL ${ARGS[*]} $OUT_IMG"
    if ! "$HYB_TOOL" "${ARGS[@]}" "$OUT_IMG"; then
      if (( UEFI == 1 )); then
        info "$HYB_TOOL failed with -u; retrying without UEFI flag"
        ARGS=( "${ARGS[@]/-u/}" )
        "$HYB_TOOL" "${ARGS[@]}" "$OUT_IMG" || err "$HYB_TOOL failed"
      else
        err "$HYB_TOOL failed"
      fi
    fi
  else
    # Fallback to xorriso if present (re-writes the file but keeps contents)
    if command -v xorriso >/dev/null 2>&1; then
      info "isohybrid not found; using xorriso to enable isohybrid"
      xorriso -indev "$IN_ISO" -outdev "$OUT_IMG" -boot_image any isohybrid=on >/dev/null 2>&1 || true
    else
      info "Warning: no isohybrid/xorriso; IMG may not be disk-bootable"
    fi
  fi

  info "Done: $OUT_IMG"
  exit 0
fi

############################
# FAT32 real disk image path
############################

[[ -n "$BUILD_ROOT" ]] || err "Missing --build-root for fat32 mode"
[[ -n "$LIST_FILE" ]] || err "Missing --list for fat32 mode"
[[ -f "$LIST_FILE" ]] || err "List file not found: $LIST_FILE"

# Resolve build root to absolute
BUILD_ROOT="$(cd "$BUILD_ROOT" && pwd)"

# Find host fatten tool
if [[ -x "$BUILD_ROOT/host-tools/bin/fatten" ]]; then
  FATTEN_BIN="$BUILD_ROOT/host-tools/bin/fatten"
elif command -v fatten >/dev/null 2>&1; then
  FATTEN_BIN="$(command -v fatten)"
else
  err "Cannot find 'fatten' host tool. Build host-tools or add it to PATH."
fi

FREELDR_SYS="$BUILD_ROOT/boot/freeldr/freeldr/freeldr.sys"
UEFILDR_EFI="$BUILD_ROOT/boot/freeldr/freeldr/uefildr.efi"
BOOT_DOSMBR="$BUILD_ROOT/boot/freeldr/bootsect/dosmbr.bin"
BOOT_FAT16="$BUILD_ROOT/boot/freeldr/bootsect/fat.bin"
BOOT_FAT32="$BUILD_ROOT/boot/freeldr/bootsect/fat32.bin"

for f in "$BOOT_DOSMBR" "$BOOT_FAT16" "$BOOT_FAT32"; do
  [[ -f "$f" ]] || err "Missing build artifact: $f"
done

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

PART_IMG="$WORKDIR/part.img"
BS_ORIG="$WORKDIR/bs.orig.bin"
BS_NEW="$WORKDIR/bs.new.bin"

# Calculate geometry
[[ "$SIZE_MIB" =~ ^[0-9]+$ ]] || err "Size must be an integer MiB"
TOTAL_SECTORS=$(( SIZE_MIB * 2048 )) # 512-byte sectors
P_START=2048                         # 1MiB alignment
P_COUNT=$(( TOTAL_SECTORS - P_START ))
(( P_COUNT > 0 )) || err "Image too small (size=$SIZE_MIB MiB)"

info "Building FAT32 image: $OUT_IMG (${SIZE_MIB} MiB)"
rm -f "$OUT_IMG"
dd if=/dev/zero of="$OUT_IMG" bs=1 count=0 seek=$(( TOTAL_SECTORS * 512 )) status=none

# Create partition table and partition
if (( UEFI_ONLY == 1 )); then
  info "Partitioning (MBR/ESP, start=$P_START, sectors=$P_COUNT, type=0xEF)"
  {
    echo 'label: dos'
    echo 'unit: sectors'
    echo
    echo "$P_START,$P_COUNT,0xEF"
  } | sfdisk "$OUT_IMG" >/dev/null
else
  info "Partitioning (MBR, start=$P_START, sectors=$P_COUNT, type=0x0C bootable)"
  {
    echo 'label: dos'
    echo 'unit: sectors'
    echo
    echo "$P_START,$P_COUNT,0x0C,*"
  } | sfdisk "$OUT_IMG" >/dev/null

  # Install ReactOS MBR boot code while preserving partition table/signature
  info "Writing MBR boot code"
  dd if="$BOOT_DOSMBR" of="$OUT_IMG" bs=440 count=1 conv=notrunc status=none
fi

# Create and format the partition volume image using fatten
info "Formatting FAT volume (sectors=$P_COUNT)"
"$FATTEN_BIN" "$PART_IMG" -format "$P_COUNT" >/dev/null

# For BIOS-bootable images, install ReactOS VBR boot code; otherwise skip (UEFI-only)
if (( UEFI_ONLY == 0 )); then
  # Read the freshly created boot sector and determine FAT type
  dd if="$PART_IMG" of="$BS_ORIG" bs=512 count=1 status=none

  is_fat32=0
  sig_fat32=$(dd if="$BS_ORIG" bs=1 skip=82 count=5 2>/dev/null | tr -d '\r\n' || true)
  sig_fat16=$(dd if="$BS_ORIG" bs=1 skip=54 count=5 2>/dev/null | tr -d '\r\n' || true)
  if [[ "$sig_fat32" == "FAT32"* ]]; then
    is_fat32=1
  elif [[ "$sig_fat16" == "FAT16"* || "$sig_fat16" == "FAT12"* ]]; then
    is_fat32=0
  else
    if (( P_COUNT >= 131072 )); then
      is_fat32=1
    fi
  fi

  if (( is_fat32 == 1 )); then
    info "Installing FAT32 VBR boot code (freeldr)"
    cp "$BOOT_FAT32" "$BS_NEW"
    dd if="$BS_ORIG" of="$BS_NEW" bs=1 skip=3 seek=3 count=87 conv=notrunc status=none
    dd if="$BOOT_FAT32" of="$PART_IMG" bs=512 skip=1 seek=14 count=1 conv=notrunc status=none
  else
    info "Installing FAT16 VBR boot code (freeldr)"
    cp "$BOOT_FAT16" "$BS_NEW"
    dd if="$BS_ORIG" of="$BS_NEW" bs=1 skip=3 seek=3 count=59 conv=notrunc status=none
  fi

  # Fix up BPB fields for a partition embedded at LBA P_START on a hard disk:
  # - HiddenSectors (offset 0x1C, 4 bytes little-endian) must equal partition start sector
  # - Media descriptor (offset 0x15, 1 byte) should be 0xF8 for fixed disks
  printf "$(printf '%08x' $P_START | sed -E 's/(..)(..)(..)(..)/\\x\4\\x\3\\x\2\\x\1/')" | dd of="$BS_NEW" bs=1 seek=$((0x1C)) count=4 conv=notrunc status=none
  printf "\xF8" | dd of="$BS_NEW" bs=1 seek=$((0x15)) count=1 conv=notrunc status=none

  dd if="$BS_NEW" of="$PART_IMG" bs=512 count=1 conv=notrunc status=none
fi

# Helpers
fat_mkdir_p() {
  local p="$1"; local cur=""; IFS='/' read -ra parts <<<"$p"; for seg in "${parts[@]}"; do [[ -z "$seg" ]] && continue; cur+="/$seg"; "$FATTEN_BIN" "$PART_IMG" -mkdir "$cur" >/dev/null 2>&1 || true; done
}

add_file() {
  local src="$1"; local dest="$2"; local ddir; ddir="$(dirname "$dest")"; fat_mkdir_p "$ddir"; "$FATTEN_BIN" "$PART_IMG" -add "$src" "$dest" >/dev/null
}

info "Copying LiveCD file set from list: $LIST_FILE"
while IFS= read -r line; do
  [[ -z "$line" ]] && continue
  if [[ "$line" != *"="* ]]; then
    # First line is the empty dir path; ignore any non-graft lines
    continue
  fi
  dest="/${line%%=*}"; src="${line#*=}"
  # Directory markers
  if [[ "$src" == */boot/empty ]]; then
    fat_mkdir_p "$dest"
    continue
  fi
  # Regular file
  if [[ -f "$src" ]]; then
    add_file "$src" "$dest"
  fi
done < "$LIST_FILE"

# Ensure EFI loader exists (it should come from the list already)
if [[ -f "$UEFILDR_EFI" ]]; then
  fat_mkdir_p "/efi/boot"
  # Add/overwrite to be safe
  add_file "$UEFILDR_EFI" "/efi/boot/bootx64.efi"
fi

# For BIOS boot, ensure FREELDR.SYS is present at root (in addition to loader/freeldr.sys)
if (( UEFI_ONLY == 0 )); then
  if [[ -f "$FREELDR_SYS" ]]; then
    add_file "$FREELDR_SYS" "/FREELDR.SYS"
  fi
fi

# Optionally add VirtIO block miniport (viostor) if provided
if [[ -n "$VIOSTOR_SRC" && -f "$VIOSTOR_SRC" ]]; then
  fat_mkdir_p "/reactos/system32/drivers"
  add_file "$VIOSTOR_SRC" "/reactos/system32/drivers/viostor.sys"
fi

info "Embedding partition into disk image"
dd if="$PART_IMG" of="$OUT_IMG" bs=512 seek=$P_START conv=notrunc status=none

info "Done: $OUT_IMG"
