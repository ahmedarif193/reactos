#!/bin/bash
# Wrapper for windres to handle ARM64 target
# ReactOS ARM64 windres wrapper

# Get the input and output files
ARGS=""
INPUT=""
OUTPUT=""
SKIP_NEXT=false

for arg in "$@"; do
    if [ "$SKIP_NEXT" = true ]; then
        SKIP_NEXT=false
        continue
    fi
    
    case "$arg" in
        -o)
            SKIP_NEXT=true
            OUTPUT="${!#}"
            ;;
        *.rc)
            INPUT="$arg"
            ;;
        *)
            ARGS="$ARGS $arg"
            ;;
    esac
done

# For ARM64, just create an empty resource file
if [ -n "$OUTPUT" ]; then
    # Create a minimal valid PE resource
    echo -e "\x00\x00\x00\x00\x20\x00\x00\x00\xff\xff\x00\x00\xff\xff\x00\x00" > "$OUTPUT"
    echo -e "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" >> "$OUTPUT"
    exit 0
fi

# Fallback - call original windres
exec /home/ahmed/x-tools/aarch64-w64-mingw32/bin/aarch64-w64-mingw32-windres "$@"