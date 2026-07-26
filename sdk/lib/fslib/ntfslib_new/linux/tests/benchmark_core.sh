#!/bin/sh
# Mountless shared-core comparison against the independent NTFS-3G tools.
# Both implementations must return identical names and bytes before any
# timing starts. Neither filesystem is ever mounted; every timed command
# starts a fresh process and volume object against a warm host page cache.
set -eu

if test "$#" -ne 1; then
    echo "usage: benchmark_core.sh NTFSLIB_FUSE_BINARY" >&2
    exit 2
fi
frontend=$1

samples=${NTFSLIB_BENCH_SAMPLES:-5}
dir_iterations=${NTFSLIB_BENCH_DIR_ITERATIONS:-100}
read_iterations=${NTFSLIB_BENCH_READ_ITERATIONS:-3}
smallread_iterations=${NTFSLIB_BENCH_SMALLREAD_ITERATIONS:-25}
frag_iterations=${NTFSLIB_BENCH_FRAG_ITERATIONS:-3}
mutate_iterations=${NTFSLIB_BENCH_MUTATE_ITERATIONS:-10}
small_file_count=${NTFSLIB_BENCH_SMALL_FILES:-250}
read_size_mib=${NTFSLIB_BENCH_READ_SIZE_MIB:-64}
frag_size_mib=${NTFSLIB_BENCH_FRAG_SIZE_MIB:-8}
mutate_size_mib=${NTFSLIB_BENCH_MUTATE_SIZE_MIB:-1}
bench_cpu=${NTFSLIB_BENCH_CPU:-0}

for tool in mkntfs ntfscp ntfsls ntfscat ntfstruncate ntfsfix taskset; do
    command -v "$tool" >/dev/null 2>&1 ||
        { echo "missing required tool: $tool" >&2; exit 2; }
done
test "$samples" -ge 5 ||
    { echo "at least five samples are required" >&2; exit 2; }
test "$smallread_iterations" -le "$small_file_count" ||
    { echo "smallread iterations exceed the small file count" >&2; exit 2; }

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-core-bench.XXXXXX")
image="$workdir/bench.ntfs"
frag_image="$workdir/frag.ntfs"
source_file="$workdir/sequential.bin"
small_source="$workdir/small.txt"
frag_source="$workdir/frag.bin"
mutate_source="$workdir/mutate.bin"
csv="$workdir/samples.csv"
succeeded=0

cleanup()
{
    if test "$succeeded" -eq 1; then
        rm -r -- "$workdir"
    else
        echo "preserved failing fixture: $workdir" >&2
    fi
}
trap cleanup EXIT HUP INT TERM

now_ns()
{
    date +%s%N
}

# One timed burst of identically launched fresh processes, pinned to one
# CPU. Prints elapsed seconds.
time_burst()
{
    _iterations=$1
    shift
    _start=$(now_ns)
    _index=0
    while test "$_index" -lt "$_iterations"; do
        taskset -c "$bench_cpu" "$@" >/dev/null
        _index=$((_index + 1))
    done
    _stop=$(now_ns)
    awk -v a="$_start" -v b="$_stop" \
        'BEGIN { printf "%.6f", (b - a) / 1e9 }'
}

# Like time_burst, but appends a distinct small-file path per launch so no
# per-name state can be reused. Prints elapsed seconds.
time_sweep()
{
    _iterations=$1
    shift
    _start=$(now_ns)
    _index=1
    while test "$_index" -le "$_iterations"; do
        taskset -c "$bench_cpu" "$@" \
            "$(printf '/small-%06d.txt' "$_index")" >/dev/null
        _index=$((_index + 1))
    done
    _stop=$(now_ns)
    awk -v a="$_start" -v b="$_stop" \
        'BEGIN { printf "%.6f", (b - a) / 1e9 }'
}

median_of()
{
    _metric_tool=$1
    awk -F, -v key="$_metric_tool" '
        $3 "," $4 == key { values[count++] = $6 }
        END {
            if (count == 0)
                exit 1
            for (i = 0; i < count; i++)
                for (j = i + 1; j < count; j++)
                    if (values[j] + 0 < values[i] + 0) {
                        swap = values[i]
                        values[i] = values[j]
                        values[j] = swap
                    }
            if (count % 2)
                printf "%.2f", values[(count - 1) / 2]
            else
                printf "%.2f", (values[count / 2 - 1] + values[count / 2]) / 2
        }' "$csv"
}

echo "# ntfslib mountless core benchmark $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# kernel: $(uname -sr)"
echo "# cpu: $(awk -F: '/^model name/ { gsub(/^ /, "", $2); print $2; exit }' \
    /proc/cpuinfo), pinned to CPU $bench_cpu"
echo "# ntfs-3g tools: $(ntfsls --version 2>&1 | head -n 1)"
echo "# fixture: 384 MiB NTFS image, $small_file_count small files," \
    "one $read_size_mib MiB ordinary stream," \
    "two $mutate_size_mib MiB mutation targets;" \
    "64 MiB fragmented image, one $frag_size_mib MiB shredded stream"
echo "# directory sample: $dir_iterations fresh enumerations;" \
    "read sample: $read_iterations fresh whole-stream reads;" \
    "smallread sample: $smallread_iterations fresh exact-lookup reads;" \
    "fragread sample: $frag_iterations fresh fragmented reads;" \
    "mutate sample: $mutate_iterations fresh $mutate_size_mib MiB" \
    "overwrites; $samples samples, alternating tool order"

truncate -s 384M "$image"
mkntfs -F -Q -q -L NTFSLIB_CORE "$image"
dd if=/dev/urandom of="$source_file" bs=1M count="$read_size_mib" \
    status=none
printf 'ntfslib core benchmark payload\n' >"$small_source"
dd if=/dev/urandom of="$mutate_source" bs=1M count="$mutate_size_mib" \
    status=none
ntfscp -f -q "$image" "$source_file" /sequential.bin
ntfscp -f -q "$image" "$mutate_source" /mutate-ntfslib.bin
ntfscp -f -q "$image" "$mutate_source" /mutate-ntfs3g.bin
file_index=1
while test "$file_index" -le "$small_file_count"; do
    ntfscp -f -q "$image" "$small_source" \
        "$(printf '/small-%06d.txt' "$file_index")"
    file_index=$((file_index + 1))
done
"$frontend" --probe "$image" | sed 's/^/# /'

# Fragmented fixture: a second image is filled with 1 MiB pads until the
# NTFS-3G allocator reports no space, every other pad is cut to 8 bytes to
# shred the free space into scattered holes, and the shared core then
# allocates /frag.bin through those holes. NTFS-3G's own allocator refuses
# this allocation outright (ENOSPC), so the fragmented layout is written by
# ntfslib and byte-verified through NTFS-3G below before any timing.
truncate -s 64M "$frag_image"
mkntfs -F -Q -q -L NTFSLIB_FRAG "$frag_image"
dd if=/dev/urandom of="$frag_source" bs=1M count="$frag_size_mib" \
    status=none
pad_index=1
while test "$pad_index" -le 70; do
    if ! ntfscp -f -q "$frag_image" "$mutate_source" \
        "$(printf '/pad-%02d.bin' "$pad_index")" 2>/dev/null; then
        break
    fi
    pad_index=$((pad_index + 1))
done
ntfsls -i -p / "$frag_image" |
    awk '$2 ~ /^pad-[0-9]*[13579]\.bin$/ { print $1 }' \
    >"$workdir/frag.holes"
while read -r pad_inode; do
    ntfstruncate -f -q "$frag_image" "$pad_inode" 0x80 '' 8 \
        >/dev/null 2>&1
done <"$workdir/frag.holes"
"$frontend" --create-file "$frag_image" /frag.bin >/dev/null
"$frontend" --write "$frag_image" /frag.bin 0 "$frag_source"
frag_extents=$("$frontend" --retrieval-pointers "$frag_image" /frag.bin 0 |
    tail -n +2 | wc -l)
test "$frag_extents" -ge 4 ||
    { echo "fragmented fixture stayed contiguous" >&2; exit 1; }
echo "# fragmented fixture: $frag_size_mib MiB in $frag_extents extents"

# Correctness gate: identical visible names and identical stream bytes
# through both implementations, before any timing. The mutation gate also
# crosses implementations: NTFS-3G reads back what ntfslib wrote and
# ntfslib reads back what ntfscp wrote.
"$frontend" --list "$image" / | awk '{ print $NF }' | sort \
    >"$workdir/names.ntfslib"
ntfsls "$image" | sort >"$workdir/names.ntfs3g"
cmp "$workdir/names.ntfslib" "$workdir/names.ntfs3g"
name_count=$(wc -l <"$workdir/names.ntfslib")
test "$name_count" -eq $((small_file_count + 3))
"$frontend" --cat "$image" /sequential.bin | cmp - "$source_file"
ntfscat "$image" /sequential.bin | cmp - "$source_file"
"$frontend" --cat "$frag_image" /frag.bin | cmp - "$frag_source"
ntfscat "$frag_image" /frag.bin | cmp - "$frag_source"
"$frontend" --write "$image" /mutate-ntfslib.bin 0 "$mutate_source"
ntfscp -f -q "$image" "$mutate_source" /mutate-ntfs3g.bin
ntfscat "$image" /mutate-ntfslib.bin | cmp - "$mutate_source"
"$frontend" --cat "$image" /mutate-ntfs3g.bin | cmp - "$mutate_source"
ntfsfix -n "$image" >/dev/null 2>&1
ntfsfix -n "$frag_image" >/dev/null 2>&1
echo "# validation: $name_count identical names, identical stream bytes," \
    "cross-implementation mutation round-trips, ntfsfix -n clean"

# One untimed warm-up per tool and workload. The mutation warm-up doubled
# as the cross-implementation gate above.
taskset -c "$bench_cpu" "$frontend" --list "$image" / >/dev/null
taskset -c "$bench_cpu" ntfsls "$image" >/dev/null
taskset -c "$bench_cpu" "$frontend" --cat "$image" /sequential.bin \
    >/dev/null
taskset -c "$bench_cpu" ntfscat "$image" /sequential.bin >/dev/null
taskset -c "$bench_cpu" "$frontend" --cat "$image" /small-000001.txt \
    >/dev/null
taskset -c "$bench_cpu" ntfscat "$image" /small-000001.txt >/dev/null
taskset -c "$bench_cpu" "$frontend" --cat "$frag_image" /frag.bin \
    >/dev/null
taskset -c "$bench_cpu" ntfscat "$frag_image" /frag.bin >/dev/null

echo "sample,order,tool,metric,seconds,rate,unit" | tee "$csv"
sample=1
while test "$sample" -le "$samples"; do
    if test $((sample % 2)) -eq 1; then
        first=ntfslib
        second=ntfs3g
    else
        first=ntfs3g
        second=ntfslib
    fi
    for tool in "$first" "$second"; do
        if test "$tool" = ntfslib; then
            dir_seconds=$(time_burst "$dir_iterations" \
                "$frontend" --list "$image" /)
            read_seconds=$(time_burst "$read_iterations" \
                "$frontend" --cat "$image" /sequential.bin)
            smallread_seconds=$(time_sweep "$smallread_iterations" \
                "$frontend" --cat "$image")
            frag_seconds=$(time_burst "$frag_iterations" \
                "$frontend" --cat "$frag_image" /frag.bin)
            mutate_seconds=$(time_burst "$mutate_iterations" \
                "$frontend" --write "$image" /mutate-ntfslib.bin 0 \
                "$mutate_source")
        else
            dir_seconds=$(time_burst "$dir_iterations" \
                ntfsls "$image")
            read_seconds=$(time_burst "$read_iterations" \
                ntfscat "$image" /sequential.bin)
            smallread_seconds=$(time_sweep "$smallread_iterations" \
                ntfscat "$image")
            frag_seconds=$(time_burst "$frag_iterations" \
                ntfscat "$frag_image" /frag.bin)
            mutate_seconds=$(time_burst "$mutate_iterations" \
                ntfscp -f -q "$image" "$mutate_source" /mutate-ntfs3g.bin)
        fi
        dir_rate=$(awk -v i="$dir_iterations" -v s="$dir_seconds" \
            'BEGIN { printf "%.2f", i / s }')
        read_rate=$(awk -v i="$read_iterations" -v m="$read_size_mib" \
            -v s="$read_seconds" 'BEGIN { printf "%.2f", i * m / s }')
        smallread_rate=$(awk -v i="$smallread_iterations" \
            -v s="$smallread_seconds" 'BEGIN { printf "%.2f", i / s }')
        frag_rate=$(awk -v i="$frag_iterations" -v m="$frag_size_mib" \
            -v s="$frag_seconds" 'BEGIN { printf "%.2f", i * m / s }')
        mutate_rate=$(awk -v i="$mutate_iterations" \
            -v m="$mutate_size_mib" -v s="$mutate_seconds" \
            'BEGIN { printf "%.2f", i * m / s }')
        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$sample" "$first-first" "$tool" directory \
            "$dir_seconds" "$dir_rate" ops/s | tee -a "$csv"
        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$sample" "$first-first" "$tool" read \
            "$read_seconds" "$read_rate" MiB/s | tee -a "$csv"
        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$sample" "$first-first" "$tool" smallread \
            "$smallread_seconds" "$smallread_rate" ops/s | tee -a "$csv"
        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$sample" "$first-first" "$tool" fragread \
            "$frag_seconds" "$frag_rate" MiB/s | tee -a "$csv"
        printf '%s,%s,%s,%s,%s,%s,%s\n' \
            "$sample" "$first-first" "$tool" mutate \
            "$mutate_seconds" "$mutate_rate" MiB/s | tee -a "$csv"
    done
    sample=$((sample + 1))
done

# The timed mutations rewrote the same bytes; both targets and both
# volumes must still be exactly consistent.
ntfscat "$image" /mutate-ntfslib.bin | cmp - "$mutate_source"
ntfscat "$image" /mutate-ntfs3g.bin | cmp - "$mutate_source"
ntfsfix -n "$image" >/dev/null 2>&1
ntfsfix -n "$frag_image" >/dev/null 2>&1

dir_ntfslib=$(median_of "ntfslib,directory")
dir_ntfs3g=$(median_of "ntfs3g,directory")
read_ntfslib=$(median_of "ntfslib,read")
read_ntfs3g=$(median_of "ntfs3g,read")
smallread_ntfslib=$(median_of "ntfslib,smallread")
smallread_ntfs3g=$(median_of "ntfs3g,smallread")
frag_ntfslib=$(median_of "ntfslib,fragread")
frag_ntfs3g=$(median_of "ntfs3g,fragread")
mutate_ntfslib=$(median_of "ntfslib,mutate")
mutate_ntfs3g=$(median_of "ntfs3g,mutate")
echo "median fresh open + directory enumeration:" \
    "ntfslib $dir_ntfslib ops/s, ntfsls $dir_ntfs3g ops/s," \
    "ratio $(awk -v a="$dir_ntfslib" -v b="$dir_ntfs3g" \
        'BEGIN { printf "%.2f", a / b }')x"
echo "median fresh open + cached sequential read:" \
    "ntfslib $read_ntfslib MiB/s, ntfscat $read_ntfs3g MiB/s," \
    "ratio $(awk -v a="$read_ntfslib" -v b="$read_ntfs3g" \
        'BEGIN { printf "%.2f", a / b }')x"
echo "median fresh open + exact lookup + resident read:" \
    "ntfslib $smallread_ntfslib ops/s, ntfscat $smallread_ntfs3g ops/s," \
    "ratio $(awk -v a="$smallread_ntfslib" -v b="$smallread_ntfs3g" \
        'BEGIN { printf "%.2f", a / b }')x"
echo "median fresh open + fragmented read ($frag_extents extents):" \
    "ntfslib $frag_ntfslib MiB/s, ntfscat $frag_ntfs3g MiB/s," \
    "ratio $(awk -v a="$frag_ntfslib" -v b="$frag_ntfs3g" \
        'BEGIN { printf "%.2f", a / b }')x"
echo "median fresh open + $mutate_size_mib MiB overwrite:" \
    "ntfslib $mutate_ntfslib MiB/s, ntfscp $mutate_ntfs3g MiB/s," \
    "ratio $(awk -v a="$mutate_ntfslib" -v b="$mutate_ntfs3g" \
        'BEGIN { printf "%.2f", a / b }')x"

succeeded=1
