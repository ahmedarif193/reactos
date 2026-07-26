#!/bin/sh
set -eu

frontend=$1
benchmark=$2
list_iterations=${NTFSLIB_LIST_ITERATIONS:-10000}
read_iterations=${NTFSLIB_READ_ITERATIONS:-200}
small_file_count=${NTFSLIB_SMALL_FILE_COUNT:-1000}
read_size_mib=${NTFSLIB_READ_SIZE_MIB:-64}
runtime_root=${NTFSLIB_BENCH_ROOT:-"$PWD"}

for tool in mkntfs ntfscp ntfs-3g fusermount3 mountpoint; do
    command -v "$tool" >/dev/null 2>&1 ||
        { echo "missing required tool: $tool" >&2; exit 2; }
done

mkdir -p "$runtime_root"
test_dir=$(mktemp -d "$runtime_root/ntfslib-fuse-bench.XXXXXX")
image="$test_dir/bench.ntfs"
source_file="$test_dir/sequential.bin"
small_source="$test_dir/small.txt"
custom_mount="$test_dir/ntfslib"
ntfs3g_mount="$test_dir/ntfs3g"

cleanup()
{
    if mountpoint -q "$custom_mount"; then
        fusermount3 -u "$custom_mount" || true
    fi
    if mountpoint -q "$ntfs3g_mount"; then
        fusermount3 -u "$ntfs3g_mount" || true
    fi
    rm -f "$small_source" "$source_file" "$image"
    rmdir "$custom_mount" "$ntfs3g_mount" "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

mkdir "$custom_mount" "$ntfs3g_mount"
truncate -s 384M "$image"
mkntfs -F -Q -q -L NTFSLIB_BENCH "$image"
dd if=/dev/zero of="$source_file" bs=1M count="$read_size_mib" status=none
printf 'ntfslib benchmark payload\n' >"$small_source"
ntfscp -f -q "$image" "$source_file" /sequential.bin

file_index=1
while test "$file_index" -le "$small_file_count"; do
    destination=$(printf '/small-%06d.txt' "$file_index")
    ntfscp -f -q "$image" "$small_source" "$destination"
    file_index=$((file_index + 1))
done

"$frontend" "$image" "$custom_mount"
ntfs-3g "$image" "$ntfs3g_mount" -o ro

attempt=0
while { ! mountpoint -q "$custom_mount" ||
        ! mountpoint -q "$ntfs3g_mount" ||
        ! test -r "$custom_mount/sequential.bin" ||
        ! test -r "$ntfs3g_mount/sequential.bin"; } &&
      test "$attempt" -lt 50; do
    sleep 0.1
    attempt=$((attempt + 1))
done
mountpoint -q "$custom_mount"
mountpoint -q "$ntfs3g_mount"

cmp "$custom_mount/sequential.bin" "$source_file"
cmp "$ntfs3g_mount/sequential.bin" "$source_file"

echo 'driver,workload,seconds,rate,items_or_bytes,rate_unit'
ntfslib_results=$("$benchmark" ntfslib \
    "$custom_mount" \
    "$custom_mount/sequential.bin" \
    "$list_iterations" \
    "$read_iterations")
ntfs3g_results=$("$benchmark" ntfs-3g \
    "$ntfs3g_mount" \
    "$ntfs3g_mount/sequential.bin" \
    "$list_iterations" \
    "$read_iterations")
printf '%s\n%s\n' "$ntfslib_results" "$ntfs3g_results"

ntfslib_entries=$(printf '%s\n' "$ntfslib_results" |
    awk -F, '$2 == "list" { print $5 }')
ntfs3g_entries=$(printf '%s\n' "$ntfs3g_results" |
    awk -F, '$2 == "list" { print $5 }')
test "$ntfslib_entries" = "$ntfs3g_entries" ||
    {
        echo "directory entry mismatch: ntfslib=$ntfslib_entries, ntfs-3g=$ntfs3g_entries" >&2
        exit 1
    }
