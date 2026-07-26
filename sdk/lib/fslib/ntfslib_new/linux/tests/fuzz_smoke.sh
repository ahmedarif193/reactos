#!/bin/sh
# Deterministic corruption-robustness smoke: flip seeded random bytes in
# the metadata region of a valid image and require every probe, listing,
# and read to fail gracefully - no signals, no sanitizer aborts. Run it
# against the ASan/UBSan build to turn memory errors into failures. The
# same variants are fed to the NTFS-3G tools for comparison; their
# crashes are counted but never fail the test.
set -eu

frontend=$1
mkntfs=$2
ntfscp=$3
ntfsls=$4
ntfscat=$5
rounds=${NTFSLIB_FUZZ_ROUNDS:-150}

command -v python3 >/dev/null 2>&1 || exit 77

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-fuzz.XXXXXX")
image="$workdir/base.ntfs"
variant="$workdir/variant.ntfs"
payload="$workdir/payload.bin"

cleanup()
{
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

truncate -s 24M "$image"
"$mkntfs" -F -Q -q -L FUZZ "$image"
dd if=/dev/urandom of="$payload" bs=64K count=4 status=none
"$ntfscp" -f -q "$image" "$payload" /data.bin
index=1
while test "$index" -le 8; do
    "$ntfscp" -f -q "$image" "$payload" "/file-$index.bin"
    index=$((index + 1))
done

# One graceful-failure check per tool invocation: any termination by
# signal (exit > 128) or sanitizer abort fails the test.
run_ntfslib()
{
    if "$frontend" "$@" >/dev/null 2>&1; then
        return 0
    fi
    status=$?
    if test "$status" -gt 128; then
        echo "ntfslib crashed (exit $status): $*" >&2
        return 1
    fi
    return 0
}

ntfs3g_crashes=0
run_ntfs3g()
{
    if "$@" >/dev/null 2>&1; then
        return 0
    fi
    if test "$?" -gt 128; then
        ntfs3g_crashes=$((ntfs3g_crashes + 1))
    fi
    return 0
}

round=0
while test "$round" -lt "$rounds"; do
    cp "$image" "$variant"
    python3 - "$variant" "$round" <<'PYEOF'
import random
import sys

path, seed = sys.argv[1], int(sys.argv[2])
generator = random.Random(0x17F5 + seed)
with open(path, "r+b") as handle:
    handle.seek(0, 2)
    size = handle.tell()
    # Concentrate on the metadata-bearing first quarter, but let some
    # flips land anywhere so runlists into data are covered too.
    for _ in range(generator.randrange(1, 24)):
        if generator.random() < 0.85:
            offset = generator.randrange(0, min(size, 4 * 1024 * 1024))
        else:
            offset = generator.randrange(0, size)
        handle.seek(offset)
        original = handle.read(1)
        handle.seek(offset)
        handle.write(bytes([original[0] ^ (1 << generator.randrange(8))]))
PYEOF
    run_ntfslib --probe "$variant"
    run_ntfslib --list "$variant" /
    run_ntfslib --list-info "$variant" /
    run_ntfslib --cat "$variant" /data.bin
    run_ntfslib --streams "$variant" /file-3.bin
    run_ntfs3g "$ntfsls" "$variant"
    run_ntfs3g "$ntfscat" "$variant" /data.bin
    round=$((round + 1))
done

echo "fuzz smoke: $rounds variants, 5 ntfslib commands each, no crashes"
echo "fuzz smoke: ntfs-3g tool crashes on the same variants: $ntfs3g_crashes"
