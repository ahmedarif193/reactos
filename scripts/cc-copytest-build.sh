#!/usr/bin/env bash
# Build source + dest FAT32+MBR data-disk images for the Cc cached-copy stress test.
#   source.img : /SRC/<random .bin files ~DATA_GB total>  + /SRCMARK.TAG
#   dest.img   : empty, /DST dir                          + /DSTMARK.TAG
# mtools only (hdiutil wedges this host). Usage:
#   DATA_GB=10 IMG_MB=12000 cc-copytest-build.sh [outdir] [src|dest|both]
set -euo pipefail
OUT="${1:-/Users/mac/working_dir/cc-copytest}"
WHICH="${2:-both}"
DATA_GB="${DATA_GB:-10}"
IMG_MB="${IMG_MB:-12000}"
OFF=$((2048*512))
mkdir -p "$OUT"
SRC="$OUT/source.img"; DST="$OUT/dest.img"; STAGE="$OUT/stage"

mk_mbr_fat32 () {  # <img> <label>
  local img="$1" label="$2"
  rm -f "$img"
  python3 - "$img" "$IMG_MB" <<'PY'
import struct,sys
img=sys.argv[1]; mb=int(sys.argv[2]); S=512
T=mb*1024*1024//S; PS=2048; PC=T-PS
open(img,"wb").truncate(T*S)
m=bytearray(512)
m[446:462]=struct.pack("<B3sB3sII",0x80,b"\xfe\xff\xff",0x0C,b"\xfe\xff\xff",PS,PC)
m[510]=0x55; m[511]=0xAA
open(img,"r+b").write(m)
PY
  mformat -i "$img@@$OFF" -F -H 2048 -v "$label" ::
}

rm -rf "$STAGE"; mkdir -p "$STAGE"

if [ "$WHICH" = "src" ] || [ "$WHICH" = "both" ]; then
  echo "[build] generating ${DATA_GB}GB random data (chunks <4GB for FAT32)..."
  rem=$((DATA_GB*1024)); n=1
  while [ "$rem" -gt 0 ]; do
    c=3072; [ "$rem" -lt 3072 ] && c=$rem
    dd if=/dev/urandom of="$STAGE/big$n.bin" bs=1m count=$c status=none
    rem=$((rem-c)); n=$((n+1))
  done
  printf 'cc-copytest source\n' > "$STAGE/SRCMARK.TAG"
  echo "[build] formatting source.img (${IMG_MB}MB)..."
  mk_mbr_fat32 "$SRC" CCSRC
  mmd -i "$SRC@@$OFF" ::/SRC
  echo "[build] mcopy random files into source (slow)..."
  mcopy -i "$SRC@@$OFF" "$STAGE"/big*.bin ::/SRC/
  mcopy -i "$SRC@@$OFF" "$STAGE/SRCMARK.TAG" ::/
  echo "[build] source contents:"; mdir -i "$SRC@@$OFF" ::/SRC
fi

if [ "$WHICH" = "dest" ] || [ "$WHICH" = "both" ]; then
  echo "[build] formatting dest.img (${IMG_MB}MB, empty)..."
  mk_mbr_fat32 "$DST" CCDST
  mmd -i "$DST@@$OFF" ::/DST
  printf 'cc-copytest dest\n' > "$STAGE/DSTMARK.TAG"
  mcopy -i "$DST@@$OFF" "$STAGE/DSTMARK.TAG" ::/
fi

rm -rf "$STAGE"
echo "[build] DONE ($WHICH): $OUT"
ls -lh "$OUT"/*.img 2>/dev/null
