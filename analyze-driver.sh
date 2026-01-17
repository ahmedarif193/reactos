#!/bin/bash
# ReactOS Driver Import Analyzer
# Analyzes a Windows .sys driver and checks which kernel functions it needs
# against the ReactOS codebase

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
REACTOS_ROOT="${REACTOS_ROOT:-$(dirname "$0")}"

# Try to find the right objdump
if [ -z "$OBJDUMP" ]; then
    for tool in /home/ahmed/mingw-toolchains/llvm-mingw-*/bin/llvm-objdump \
                /home/ahmed/mingw-toolchains/llvm-mingw-*/bin/aarch64-w64-mingw32-objdump \
                /usr/bin/x86_64-w64-mingw32-objdump \
                llvm-objdump objdump; do
        if command -v "$tool" >/dev/null 2>&1; then
            OBJDUMP="$tool"
            break
        fi
    done
fi

# Try to find the right readobj
if [ -z "$READOBJ" ]; then
    for tool in /home/ahmed/mingw-toolchains/llvm-mingw-*/bin/llvm-readobj \
                llvm-readobj; do
        if command -v "$tool" >/dev/null 2>&1; then
            READOBJ="$tool"
            break
        fi
    done
fi

usage() {
    echo "Usage: $0 <driver.sys> [output_report.txt]"
    echo ""
    echo "Analyzes a Windows driver and checks kernel function availability in ReactOS"
    echo ""
    echo "Arguments:"
    echo "  driver.sys         Path to the Windows driver file"
    echo "  output_report.txt  Optional: Output report file (default: driver-analysis.txt)"
    echo ""
    echo "Environment variables:"
    echo "  REACTOS_ROOT       ReactOS source root (default: script directory)"
    echo "  OBJDUMP            objdump tool to use (default: llvm-objdump)"
    echo "  READOBJ            readobj tool to use (default: llvm-readobj)"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

DRIVER_FILE="$1"
REPORT_FILE="${2:-driver-analysis.txt}"

if [ ! -f "$DRIVER_FILE" ]; then
    echo -e "${RED}Error: Driver file '$DRIVER_FILE' not found${NC}"
    exit 1
fi

if [ ! -d "$REACTOS_ROOT" ]; then
    echo -e "${RED}Error: ReactOS root '$REACTOS_ROOT' not found${NC}"
    exit 1
fi

echo -e "${BLUE}=== ReactOS Driver Import Analyzer ===${NC}"
echo -e "Driver:       ${YELLOW}$DRIVER_FILE${NC}"
echo -e "ReactOS Root: ${YELLOW}$REACTOS_ROOT${NC}"
echo -e "Report File:  ${YELLOW}$REPORT_FILE${NC}"
echo ""

# Detect architecture
echo -e "${BLUE}[*] Detecting driver architecture...${NC}"
ARCH_INFO=$($OBJDUMP -f "$DRIVER_FILE" 2>/dev/null | grep "file format" || echo "unknown")
echo "Architecture: $ARCH_INFO"
echo ""

# Extract imports using objdump/readobj
echo -e "${BLUE}[*] Extracting imported functions...${NC}"
TEMP_IMPORTS=$(mktemp)

# Try llvm-readobj first if available
if [ ! -z "$READOBJ" ] && command -v "$READOBJ" >/dev/null 2>&1; then
    $READOBJ --coff-imports "$DRIVER_FILE" 2>/dev/null > "$TEMP_IMPORTS" && echo "Using llvm-readobj" || true
fi

# If that didn't work or file is empty, try objdump
if [ ! -s "$TEMP_IMPORTS" ]; then
    echo "Using objdump to extract imports..."
    $OBJDUMP -p "$DRIVER_FILE" 2>/dev/null > "$TEMP_IMPORTS" || {
        echo -e "${RED}Error: Failed to extract imports with objdump${NC}"
        rm -f "$TEMP_IMPORTS"
        exit 1
    }
fi

# Parse imports and organize by DLL
declare -A IMPORTS
CURRENT_DLL=""

# Parse the import table
while IFS= read -r line; do
    # Check for DLL name in objdump format (e.g., "DLL Name: ntoskrnl.exe")
    if echo "$line" | grep -qiE "DLL Name:\s*(ntoskrnl|hal|win32k|ndis|ksecdd|cng|fltmgr|tcpip)"; then
        CURRENT_DLL=$(echo "$line" | sed -E 's/.*DLL Name:\s*([a-zA-Z0-9_]+).*/\1/i' | tr '[:upper:]' '[:lower:]')
        continue
    fi

    # Check for llvm-readobj format (e.g., "Name: ntoskrnl.exe")
    if echo "$line" | grep -qiE "^\s*Name:\s*(ntoskrnl|hal|win32k|ndis|ksecdd|cng|fltmgr|tcpip)" && [ -z "$CURRENT_DLL" ]; then
        CURRENT_DLL=$(echo "$line" | sed -E 's/.*Name:\s*([a-zA-Z0-9_]+).*/\1/i' | tr '[:upper:]' '[:lower:]')
        continue
    fi

    # Check for function name in objdump format (indented function names)
    # Format: "        IoCreateDevice" or "        00000123 IoCreateDevice"
    if [ ! -z "$CURRENT_DLL" ]; then
        FUNC=$(echo "$line" | sed -nE 's/^\s+([0-9a-f]+\s+)?([A-Z][a-zA-Z0-9_@]+)\s*$/\2/p')
        if [ ! -z "$FUNC" ]; then
            IMPORTS["$CURRENT_DLL"]+="$FUNC"$'\n'
            continue
        fi
    fi

    # Check for function name in llvm-readobj format
    if echo "$line" | grep -qE "^\s+(Symbol|Name|Function):\s+[A-Za-z_]"; then
        FUNC=$(echo "$line" | sed -E 's/^\s+(Symbol|Name|Function):\s+([A-Za-z_][A-Za-z0-9_@]*).*/\2/')
        if [ ! -z "$FUNC" ] && [ ! -z "$CURRENT_DLL" ]; then
            IMPORTS["$CURRENT_DLL"]+="$FUNC"$'\n'
        fi
    fi
done < "$TEMP_IMPORTS"

# If parsing failed, try alternative method
if [ ${#IMPORTS[@]} -eq 0 ]; then
    echo -e "${YELLOW}Trying alternative import extraction...${NC}"

    # Extract function names directly
    FUNCS=$(grep -oE "\b[A-Z][a-zA-Z0-9_@]{3,}\b" "$TEMP_IMPORTS" | sort -u)

    # Categorize by common patterns
    for func in $FUNCS; do
        if echo "$func" | grep -qE "^(Io|Ke|Mm|Ob|Ps|Ex|Rtl|Nt|Zw|Cc|Cm|Se|Po|Pf|Vf|Lsa)"; then
            IMPORTS["ntoskrnl"]+="$func"$'\n'
        elif echo "$func" | grep -qE "^(Hal|Kd|READ_PORT|WRITE_PORT)"; then
            IMPORTS["hal"]+="$func"$'\n'
        elif echo "$func" | grep -qE "^(Ndis|Eth)"; then
            IMPORTS["ndis"]+="$func"$'\n'
        elif echo "$func" | grep -qE "^(Flt)"; then
            IMPORTS["fltmgr"]+="$func"$'\n'
        fi
    done
fi

rm -f "$TEMP_IMPORTS"

echo -e "${GREEN}Found imports from ${#IMPORTS[@]} kernel modules${NC}"
echo ""

# Start report
{
    echo "=============================================="
    echo "ReactOS Driver Import Analysis Report"
    echo "=============================================="
    echo ""
    echo "Driver File: $DRIVER_FILE"
    echo "Analysis Date: $(date)"
    echo "Architecture: $ARCH_INFO"
    echo "ReactOS Source: $REACTOS_ROOT"
    echo ""
    echo "=============================================="
    echo ""
} > "$REPORT_FILE"

# Statistics
TOTAL_FUNCTIONS=0
FOUND_FUNCTIONS=0
MISSING_FUNCTIONS=0
STUBBED_FUNCTIONS=0

# Search ReactOS source for each function
for DLL in "${!IMPORTS[@]}"; do
    echo -e "${BLUE}[*] Analyzing imports from: ${YELLOW}${DLL}.exe/dll${NC}"

    {
        echo ""
        echo "=== Imports from: ${DLL}.exe ======="
        echo ""
    } >> "$REPORT_FILE"

    # Get unique functions for this DLL
    FUNCS=$(echo "${IMPORTS[$DLL]}" | sort -u | grep -v '^$')

    while IFS= read -r func; do
        [ -z "$func" ] && continue

        TOTAL_FUNCTIONS=$((TOTAL_FUNCTIONS + 1))

        # Search for function definition in ReactOS source
        # Look for common patterns: NTSTATUS NTAPI FuncName, VOID NTAPI FuncName, etc.
        SEARCH_RESULTS=$(grep -rn --include="*.c" --include="*.cpp" --include="*.h" \
            -E "(NTSTATUS|VOID|BOOLEAN|ULONG|PVOID|HANDLE|NTKERNELAPI|NTHALAPI|NTAPI|FASTCALL|STDCALL)\s+(NTAPI|FASTCALL|STDCALL|WINAPI)?\s*${func}\s*\(" \
            "$REACTOS_ROOT" 2>/dev/null | head -5 || echo "")

        if [ ! -z "$SEARCH_RESULTS" ]; then
            # Check if it's a stub
            IS_STUB=$(echo "$SEARCH_RESULTS" | grep -i "unimplemented\|stub\|fixme" || echo "")

            if [ ! -z "$IS_STUB" ]; then
                echo -e "  ${YELLOW}[STUB]${NC} $func"
                STUBBED_FUNCTIONS=$((STUBBED_FUNCTIONS + 1))
                {
                    echo "[STUBBED] $func"
                    echo "$SEARCH_RESULTS" | head -3
                    echo ""
                } >> "$REPORT_FILE"
            else
                echo -e "  ${GREEN}[FOUND]${NC} $func"
                FOUND_FUNCTIONS=$((FOUND_FUNCTIONS + 1))
                {
                    echo "[FOUND] $func"
                    echo "$SEARCH_RESULTS" | head -3
                    echo ""
                } >> "$REPORT_FILE"
            fi
        else
            # Try alternative search for exports or declarations
            ALT_SEARCH=$(grep -rn --include="*.c" --include="*.h" --include="*.spec" \
                -w "${func}" "$REACTOS_ROOT" 2>/dev/null | head -3 || echo "")

            if [ ! -z "$ALT_SEARCH" ]; then
                echo -e "  ${YELLOW}[DECL]${NC} $func (declaration/export found)"
                FOUND_FUNCTIONS=$((FOUND_FUNCTIONS + 1))
                {
                    echo "[DECLARED] $func"
                    echo "$ALT_SEARCH"
                    echo ""
                } >> "$REPORT_FILE"
            else
                echo -e "  ${RED}[MISS]${NC} $func"
                MISSING_FUNCTIONS=$((MISSING_FUNCTIONS + 1))
                {
                    echo "[MISSING] $func"
                    echo "  *** NOT FOUND IN REACTOS SOURCE ***"
                    echo ""
                } >> "$REPORT_FILE"
            fi
        fi
    done <<< "$FUNCS"

    echo ""
done

# Summary
echo -e "${BLUE}=== Summary ===${NC}"
echo -e "Total Functions:   ${YELLOW}$TOTAL_FUNCTIONS${NC}"
echo -e "Found/Implemented: ${GREEN}$FOUND_FUNCTIONS${NC}"
echo -e "Stubbed:           ${YELLOW}$STUBBED_FUNCTIONS${NC}"
echo -e "Missing:           ${RED}$MISSING_FUNCTIONS${NC}"

COVERAGE=$((FOUND_FUNCTIONS * 100 / (TOTAL_FUNCTIONS > 0 ? TOTAL_FUNCTIONS : 1)))
echo -e "Coverage:          ${BLUE}${COVERAGE}%${NC}"
echo ""

{
    echo ""
    echo "=============================================="
    echo "SUMMARY"
    echo "=============================================="
    echo ""
    echo "Total Functions Imported:    $TOTAL_FUNCTIONS"
    echo "Found/Implemented:           $FOUND_FUNCTIONS"
    echo "Stubbed (needs work):        $STUBBED_FUNCTIONS"
    echo "Missing (not implemented):   $MISSING_FUNCTIONS"
    echo "Coverage:                    ${COVERAGE}%"
    echo ""
    echo "=============================================="
} >> "$REPORT_FILE"

echo -e "${GREEN}Report saved to: $REPORT_FILE${NC}"

# Return non-zero if there are missing functions
[ $MISSING_FUNCTIONS -eq 0 ]
