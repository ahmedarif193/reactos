#!/usr/bin/env python3
"""
Symbolize ReactOS ARM64 boot log stack traces.

Usage:
    python3 symbolize_bt.py <log_file_path>

Example:
    python3 symbolize_bt.py ../vm_monitor_output.log
"""

import re
import sys
import subprocess
from pathlib import Path


class SymbolResolver:
    """Resolves addresses to function names using ntoskrnl.exe symbols."""

    def __init__(self, ntoskrnl_path):
        self.ntoskrnl_path = ntoskrnl_path
        self.functions = []
        self._load_symbols()

    def _load_symbols(self):
        """Load all function symbols from ntoskrnl.exe using llvm-objdump."""
        print(f"Loading symbols from {self.ntoskrnl_path}...", file=sys.stderr, flush=True)

        try:
            result = subprocess.run(
                ['llvm-objdump', '-t', str(self.ntoskrnl_path)],
                capture_output=True,
                text=True,
                check=True
            )
        except subprocess.CalledProcessError as e:
            print(f"Error: Failed to run llvm-objdump: {e}", file=sys.stderr)
            sys.exit(1)
        except FileNotFoundError:
            print("Error: llvm-objdump not found. Make sure LLVM tools are in PATH.", file=sys.stderr)
            sys.exit(1)

        # Parse objdump output for function symbols
        # Format: [nnn](sec  X)(fl 0xNN)(ty  20)(scl   2) (nx N) 0xADDRESS FunctionName
        function_count = 0
        for line in result.stdout.splitlines():
            # Filter for function symbols: (ty  20) indicates a function
            if '(ty  20)(scl' not in line:
                continue

            # Extract address and function name
            match = re.search(r'0x([0-9a-f]+)\s+(\S+)\s*$', line, re.IGNORECASE)
            if match:
                addr = int(match.group(1), 16)
                name = match.group(2)
                self.functions.append((addr, name))
                function_count += 1

        # Sort by address for binary search
        self.functions.sort()
        print(f"Loaded {function_count} function symbols.", file=sys.stderr, flush=True)

    def resolve(self, address):
        """
        Resolve an address to a function name and offset.

        Args:
            address: Integer address to resolve

        Returns:
            Tuple of (function_name, offset) or ("??", address) if not found
        """
        # Find the largest function address <= target address
        prev_name = "??"
        prev_addr = 0

        for addr, name in self.functions:
            if addr <= address:
                prev_addr = addr
                prev_name = name
            else:
                break

        if prev_name == "??":
            return ("??", address)

        offset = address - prev_addr
        return (prev_name, offset)

    def format_symbol(self, address):
        """Format address as 'FunctionName+0xOffset' or '??+0xAddress'."""
        name, offset = self.resolve(address)
        if offset == 0:
            return name
        return f"{name}+0x{offset:x}"


def parse_stack_traces(log_content):
    """
    Parse stack traces from boot log.

    Looks for lines like:
        [FFFFF88003CFB490] <ntoskrnl.exe:7bdd0>
        [        11.775321] <ntoskrnl.exe:15c704>

    Returns:
        List of (full_line, module, offset) tuples
    """
    traces = []

    # Match patterns like: <ntoskrnl.exe:HEXADDR> or <hal.dll:HEXADDR>
    pattern = re.compile(r'<(\w+\.\w+):([0-9a-f]+)>', re.IGNORECASE)

    for line in log_content.splitlines():
        match = pattern.search(line)
        if match:
            module = match.group(1)
            offset_str = match.group(2)
            offset = int(offset_str, 16)
            traces.append((line, module, offset))

    return traces


def symbolize_log(log_path, ntoskrnl_path):
    """
    Symbolize all stack traces in a boot log.

    Args:
        log_path: Path to the boot log file
        ntoskrnl_path: Path to ntoskrnl.exe
    """
    # Read log file
    try:
        with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
            log_content = f.read()
    except FileNotFoundError:
        print(f"Error: Log file not found: {log_path}", file=sys.stderr)
        sys.exit(1)

    # Parse stack traces
    traces = parse_stack_traces(log_content)

    if not traces:
        print("No stack traces found in log file.", file=sys.stderr)
        return

    print(f"Found {len(traces)} stack trace entries.", file=sys.stderr, flush=True)

    # Group traces by module
    modules = {}
    for line, module, offset in traces:
        if module not in modules:
            modules[module] = []
        modules[module].append((line, offset))

    # Symbolize ntoskrnl.exe traces
    if 'ntoskrnl.exe' in modules:
        # Load symbols (this prints loading status to stderr)
        resolver = SymbolResolver(ntoskrnl_path)

        # Print results to stdout
        print()
        print("=" * 80)
        print("SYMBOLIZED STACK TRACE")
        print("=" * 80)

        for line, offset in modules['ntoskrnl.exe']:
            symbol = resolver.format_symbol(offset)

            # Extract the frame address if present
            frame_match = re.search(r'\[([0-9A-Fa-f]+)\]', line)
            if frame_match:
                frame_addr = frame_match.group(1)
                print(f"[{frame_addr}] {symbol}")
            else:
                print(f"            {symbol}")

        print("=" * 80)
        print()

    # Report other modules
    other_modules = [m for m in modules.keys() if m != 'ntoskrnl.exe']
    if other_modules:
        print(f"Note: Found traces from other modules: {', '.join(other_modules)}", file=sys.stderr)
        print(f"      Only ntoskrnl.exe is currently symbolized.", file=sys.stderr)


def main():
    if len(sys.argv) != 2:
        print("Usage: python3 symbolize_bt.py <log_file_path>", file=sys.stderr)
        print("", file=sys.stderr)
        print("Example:", file=sys.stderr)
        print("  python3 symbolize_bt.py ../vm_monitor_output.log", file=sys.stderr)
        sys.exit(1)

    log_path = Path(sys.argv[1])

    # Find ntoskrnl.exe relative to script location
    script_dir = Path(__file__).parent
    ntoskrnl_path = script_dir.parent / 'output-MinGW-arm64-Debug' / 'ntoskrnl' / 'ntoskrnl.exe'

    if not ntoskrnl_path.exists():
        print(f"Error: ntoskrnl.exe not found at {ntoskrnl_path}", file=sys.stderr)
        print(f"       Make sure you've built the kernel first.", file=sys.stderr)
        sys.exit(1)

    symbolize_log(log_path, ntoskrnl_path)


if __name__ == '__main__':
    main()
