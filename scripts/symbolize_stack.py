#!/usr/bin/env python3
"""Translate ReactOS kd stack offsets to symbol names.

Usage examples
--------------
    ./symbolize_stack.py /tmp/outamd.log
    ./symbolize_stack.py --binary ntoskrnl.exe=ntoskrnl/ntoskrnl.exe /tmp/outamd.log

When no filtering options are supplied the script assumes the input is a kd log,
trims everything before the first "Entered debugger" marker, keeps only the
lines that start with "[", echoes those lines, and prints symbolized entries
for each ``<module:offset>`` token that appears on them.

Pass ``--tail-from`` and/or ``--only-prefix`` to override the default trimming,
``--binary`` to provide additional module-to-binary mappings, and ``--show-line``
if you want the original lines when custom filters are in use.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

SUFFIX_WHITELIST = {".exe", ".dll", ".sys", ".efi", ".ax", ".acm", ".drv", ".so"}

NM_TOOL = os.environ.get("NM", "x86_64-w64-mingw32-nm")
_PATTERN = re.compile(r"<([^:<>]+):([0-9A-Fa-f]+)>")

TAIL_DEFAULT = 2
DEFAULT_TIMEOUT_SECONDS = 60
DEFAULT_LOG_PATH = Path("/tmp/out-q35.log")

# Convenience: ready-to-run QEMU command for this build/ISO
DEFAULT_QEMU_CMD = (
    "qemu-system-x86_64 -enable-kvm "
    "-M q35,usb=off "
    "-m 3G "
    "-drive file=livecd_eotics.com_reactosv2.iso,media=cdrom "
    "-device qemu-xhci,id=xhci "
    "-serial stdio "
    "-device qemu-xhci "
    "-device usb-kbd "
    "-device usb-tablet "
    "-display none"
)


def _locate_build_dir() -> Path:
    """Return the directory that contains the ReactOS build artifacts."""

    env_override = os.environ.get("SYMBOLIZE_BUILD_DIR")
    if env_override:
        candidate = Path(env_override).expanduser().resolve()
        if (candidate / "build.ninja").exists():
            return candidate
        raise FileNotFoundError(
            f"SYMBOLIZE_BUILD_DIR={candidate} does not contain build.ninja"
        )

    cwd = Path.cwd().resolve()
    if (cwd / "build.ninja").exists():
        return cwd

    script_parent = Path(__file__).resolve().parent
    default_candidates = [
        script_parent,
        script_parent / "output-MinGW-amd64-Release",
        script_parent / "output-MinGW-x86-Release",
        script_parent / "output-MinGW-i386-Release",
        script_parent / "output-MinGW-amd64-Debug",
        script_parent / "output-MinGW-i386-Debug",
    ]

    for candidate in default_candidates:
        candidate = candidate.resolve()
        if (candidate / "build.ninja").exists():
            return candidate

    raise FileNotFoundError(
        "Could not locate build.ninja; set SYMBOLIZE_BUILD_DIR or run from the build directory"
    )

def _get_image_base(path: Path) -> int:
    """Return the PE ImageBase for *path*.

    Supports 32-bit (PE32) and 64-bit (PE32+) binaries.
    """
    with path.open('rb') as f:
        data = f.read(0x1000)
    if len(data) < 0x40:
        raise ValueError(f"File too small to be a PE binary: {path}")
    pe_offset = int.from_bytes(data[0x3C:0x40], 'little')
    if len(data) < pe_offset + 0x80:
        # Re-read enough to cover the optional header in case it sits beyond 0x1000.
        with path.open('rb') as f:
            data = f.read(pe_offset + 0x200)
    if data[pe_offset:pe_offset + 4] != b'PE\0\0':
        raise ValueError(f"Missing PE signature in {path}")
    optional_offset = pe_offset + 4 + 20
    magic = int.from_bytes(data[optional_offset:optional_offset + 2], 'little')
    if magic == 0x10B:  # PE32
        base_offset = optional_offset + 28  # 0x1C
        base_size = 4
    elif magic == 0x20B:  # PE32+
        base_offset = optional_offset + 24  # 0x18
        base_size = 8
    else:
        raise ValueError(f"Unsupported PE optional header magic 0x{magic:X} in {path}")
    base_slice = data[base_offset:base_offset + base_size]
    if len(base_slice) != base_size:
        raise ValueError(f"Incomplete ImageBase in {path}")
    return int.from_bytes(base_slice, 'little')



@dataclass
class SymbolEntry:
    address: int
    name: str


class SymbolTable:
    def __init__(self, module: str, path: Path) -> None:
        self.module = module
        self.path = path
        self._entries: List[SymbolEntry] = []
        self._addresses: List[int] = []
        self._load()

    def _load(self) -> None:
        if not self.path.exists():
            raise FileNotFoundError(f"Binary for {self.module} not found: {self.path}")
        cmd = [NM_TOOL, "--defined-only", "-n", str(self.path)]
        try:
            output = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(f"nm failed for {self.path}: {exc}") from exc

        image_base = _get_image_base(self.path)
        seen: set[str] = set()
        for line in output.splitlines():
            parts = line.strip().split(None, 2)
            if len(parts) < 3:
                continue
            addr_str, _sym_type, name = parts
            if not re.fullmatch(r"[0-9A-Fa-f]+", addr_str):
                continue
            addr = int(addr_str, 16)
            rva = addr - image_base
            if rva < 0:
                continue
            if name in seen:
                continue
            seen.add(name)
            self._entries.append(SymbolEntry(rva, name))
            self._addresses.append(rva)

    def resolve(self, rva: int) -> Optional[Tuple[str, int]]:
        if not self._entries:
            return None
        idx = bisect_right(self._addresses, rva) - 1
        if idx < 0:
            return None
        entry = self._entries[idx]
        return entry.name, rva - entry.address


class Symbolizer:
    def __init__(self, module_map: Dict[str, Path]) -> None:
        self._tables: Dict[str, SymbolTable] = {}
        self._module_map = {k.lower(): v for k, v in module_map.items()}
        self._not_found: set[str] = set()
        self._file_index: Optional[Dict[str, Path]] = None

    def _ensure_index(self) -> None:
        if self._file_index is not None:
            return
        index: Dict[str, Path] = {}
        for path in Path.cwd().rglob('*'):
            try:
                is_file = path.is_file()
            except OSError:
                continue
            if not is_file:
                continue
            suffix = path.suffix.lower()
            if suffix and suffix not in SUFFIX_WHITELIST:
                continue
            name = path.name.lower()
            index.setdefault(name, path.resolve())
        self._file_index = index

    def _lookup_path(self, module: str) -> Optional[Path]:
        key = module.lower()
        if key in self._module_map:
            return self._module_map[key]
        if key in self._not_found:
            return None

        stem = module.lower().split('.', 1)[0]
        candidates = [Path(stem) / module, Path(stem) / module.lower(), Path(stem.upper()) / module]
        for candidate in candidates:
            candidate_path = candidate.resolve()
            if candidate_path.exists():
                resolved = candidate_path
                self._module_map[key] = resolved
                return resolved

        self._ensure_index()
        resolved = None
        if self._file_index is not None:
            resolved = self._file_index.get(key)
        if resolved is not None:
            self._module_map[key] = resolved
            return resolved

        self._not_found.add(key)
        return None

    def symbolize(self, module: str, offset: int) -> Optional[Tuple[str, int]]:
        key = module.lower()
        table = self._tables.get(key)
        if table is None:
            path = self._lookup_path(module)
            if path is None:
                return None
            table = SymbolTable(module, path)
            self._tables[key] = table
        return table.resolve(offset)


def parse_module_specs(specs: Iterable[str]) -> Dict[str, Path]:
    result: Dict[str, Path] = {}
    for spec in specs:
        if "=" in spec:
            module, path = spec.split("=", 1)
            module = module.strip()
        else:
            path = spec
            module = Path(spec).name
        result[module.lower()] = Path(path).resolve()
    return result


def should_emit_line(line: str, prefixes: Optional[Sequence[str]]) -> bool:
    if not prefixes:
        return True
    stripped = line.lstrip()
    for prefix in prefixes:
        if stripped.startswith(prefix):
            return True
    return False


def _read_lines_from_path(path: Path) -> List[str]:
    if not path.exists():
        raise FileNotFoundError(f"Log file not found: {path}")
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        return [line.rstrip("\n") for line in handle]


def _run_default_capture(log_path: Path, timeout: int, bootmain_limit: Optional[int]) -> List[str]:
    if log_path.exists():
        log_path.unlink()

    log_path = log_path.resolve()
    build_dir = _locate_build_dir()

    print(f"[symbolize] Using build directory: {build_dir}")
    print("[symbolize] Building livecd_eotics.com_reactosv2.iso …")
    build = subprocess.run(
        ["ninja", "livecd"],
        cwd=build_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if build.returncode != 0:
        print(build.stdout)
        print(build.stderr, file=sys.stderr)
        raise subprocess.CalledProcessError(build.returncode, build.args)
    print("[symbolize] Build complete.")

    livecd = build_dir / "livecd_eotics.com_reactosv2.iso"
    if not livecd.exists():
        raise FileNotFoundError("livecd_eotics.com_reactosv2.iso not found; run the script from the build directory")

    qemu_cmd = [
        "qemu-system-x86_64",
        "-enable-kvm",
        "-M",
        "q35,usb=off",
        "-m",
        "3G",
        "-drive",
        f"file={livecd},media=cdrom",
        "-device",
        "qemu-xhci,id=xhci",
        "-serial",
        f"file:{log_path}",
        # Attach both input devices explicitly to the primary xHCI root hub.
        "-device",
        "usb-kbd,bus=xhci.0,port=1",
        "-device",
        "usb-tablet,bus=xhci.0,port=2",
    ]

    print(f"[symbolize] Launching QEMU (headless, 3G RAM). Log: {log_path}")
    proc = subprocess.Popen(
        qemu_cmd,
        cwd=build_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    start_time = time.monotonic()
    stop_reason: Optional[str] = None
    seen_lines = 0
    lines_cache: List[str] = []
    poll_interval = 0.5
    # Treat a 4-second log silence as a stall to keep runs short and stop before
    # the later PnP storm territory.
    idle_threshold = 4.0

    entered_debugger_at: Optional[float] = None
    bootmain_count = 0
    bootmain_truncate_at: Optional[int] = None
    bootmain_threshold = bootmain_limit or 0

    # PnP storm detection: abort if PiQueueDeviceAction spam appears too often.
    pnp_storm_threshold = 70
    piqueue_count = 0

    last_activity = start_time

    try:
        while True:
            if log_path.exists():
                current_lines = _read_lines_from_path(log_path)
                # Hard line-count cap: if the log grows too large, stop QEMU to
                # avoid wasting time in late-boot storms.
                if stop_reason is None and len(current_lines) >= 2400:
                    stop_reason = "line-limit"
                    print(
                        f"[symbolize] Log exceeded {len(current_lines)} lines "
                        f"(limit 2400); stopping QEMU."
                    )
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    lines_cache = current_lines
                    break
                if len(current_lines) > seen_lines:
                    new_lines = current_lines[seen_lines:]
                    for idx, line in enumerate(new_lines):
                        if bootmain_threshold and "BootMain() called" in line:
                            bootmain_count += 1
                            if bootmain_count >= bootmain_threshold and bootmain_truncate_at is None:
                                bootmain_truncate_at = len(current_lines) - len(new_lines) + idx
                                stop_reason = "bootmain-repeat"
                                print(
                                    f"[symbolize] BootMain() hit {bootmain_count} times (limit {bootmain_threshold}); "
                                    "stopping QEMU and truncating log."
                                )
                                proc.terminate()
                                try:
                                    proc.wait(timeout=5)
                                except subprocess.TimeoutExpired:
                                    proc.kill()
                                break
                        if "PiQueueDeviceAction: allocated" in line:
                            piqueue_count += 1
                            if piqueue_count >= pnp_storm_threshold and stop_reason is None:
                                stop_reason = "piqueue"
                        if "Assertion failed" in line:
                            stop_reason = "assertion"
                        elif _PATTERN.search(line):
                            stop_reason = "backtrace"
                        elif "Entered debugger" in line and entered_debugger_at is None:
                            entered_debugger_at = time.monotonic()
                            print("[symbolize] 'Entered debugger' detected; waiting 1s before stopping QEMU …")
                        if stop_reason:
                            if stop_reason == "assertion":
                                print("[symbolize] Assertion detected; stopping QEMU.")
                            elif stop_reason == "piqueue":
                                print(
                                    f"[symbolize] PiQueueDeviceAction storm "
                                    f"({piqueue_count} hits); stopping QEMU."
                                )
                            else:
                                print("[symbolize] Backtrace marker detected; stopping QEMU.")
                            proc.terminate()
                            try:
                                proc.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                proc.kill()
                            break
                    lines_cache = current_lines
                    seen_lines = len(current_lines)
                    last_activity = time.monotonic()
                if stop_reason:
                    break
            if entered_debugger_at is not None and stop_reason is None:
                if time.monotonic() - entered_debugger_at >= 1.0:
                    stop_reason = "entered-debugger"
                    print("[symbolize] Stopping QEMU after debugger entry.")
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    lines_cache = _read_lines_from_path(log_path) if log_path.exists() else []
                    break
            if seen_lines > 0 and stop_reason is None:
                if time.monotonic() - last_activity >= idle_threshold:
                    print(f"[symbolize] No new log lines for {idle_threshold:.1f}s; stopping QEMU.")
                    stop_reason = "idle"
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    lines_cache = _read_lines_from_path(log_path) if log_path.exists() else []
                    break

            if proc.poll() is not None:
                lines_cache = _read_lines_from_path(log_path) if log_path.exists() else []
                break
            if timeout and (time.monotonic() - start_time) > timeout:
                print("[symbolize] Timeout reached; stopping QEMU.")
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                lines_cache = _read_lines_from_path(log_path) if log_path.exists() else []
                break
            time.sleep(poll_interval)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    if not lines_cache and log_path.exists():
        lines_cache = _read_lines_from_path(log_path)

    if bootmain_truncate_at is not None and bootmain_truncate_at >= 0:
        lines_cache = lines_cache[:bootmain_truncate_at]
        if log_path.exists():
            with log_path.open("w", encoding="utf-8") as handle:
                for line in lines_cache:
                    handle.write(line + "\n")

    print("[symbolize] QEMU run finished.")
    print(f"[symbolize] Log available at {log_path}")
    return lines_cache


def _filter_lines(lines: Sequence[str], marker: Optional[str], prefixes: Optional[Sequence[str]]) -> List[str]:
    filtered: List[str] = []
    emitting = marker is None
    for line in lines:
        if not emitting:
            if marker and marker in line:
                emitting = True
            else:
                continue
        if not should_emit_line(line, prefixes):
            continue
        filtered.append(line)
    return filtered


def _extract_last_block(lines: Sequence[str]) -> List[str]:
    last_block: List[str] = []
    current: List[str] = []
    for line in lines:
        if _PATTERN.search(line):
            current.append(line)
        else:
            if current:
                last_block = current
                current = []
    if current:
        last_block = current
    return last_block


def _print_tail(lines: Sequence[str], count: int) -> None:
    print(f"---- Last {min(count, len(lines))} log lines ----")
    for line in lines[-count:]:
        print(line)


def _symbolize_block(lines: Sequence[str], symbolizer: Symbolizer, show_line: bool) -> None:
    if not lines:
        print("---- No backtrace markers found ----")
        return

    print("---- Symbolized backtrace ----")
    for line in lines:
        matches = list(_PATTERN.finditer(line))
        if show_line:
            print(line)
        for match in matches:
            module, offset_hex = match.group(1), match.group(2)
            try:
                offset = int(offset_hex, 16)
            except ValueError:
                print(f"{module}:{offset_hex} -> <invalid offset>")
                continue
            resolved = symbolizer.symbolize(module, offset)
            if resolved is None:
                print(f"{module}+0x{offset_hex.lower()}")
                continue
            name, delta = resolved
            suffix = f"+0x{delta:X}" if delta else ""
            print(f"{module}!{name}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Translate kd stack offsets to symbols")
    parser.add_argument("--binary", "-b", action="append", default=[], help="Module to binary mapping (module=path)")
    parser.add_argument("--show-line", action="store_true", help="Echo each input line before resolved entries")
    parser.add_argument("--tail-from", help="Discard input until this marker text is seen")
    parser.add_argument("--only-prefix", action="append", help="Only process lines that begin with this prefix (whitespace ignored)")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS, help="Timeout (seconds) for the default QEMU run")
    parser.add_argument(
        "--bootmain-limit",
        type=int,
        default=None,
        help="Stop the default capture after this many 'BootMain() called' lines (omit to disable)",
    )
    parser.add_argument("input", nargs="?", help="Log file path. Omit to build and boot automatically.")

    args = parser.parse_args()

    prefixes = list(args.only_prefix) if args.only_prefix else []
    marker = args.tail_from
    apply_defaults = marker is None and not prefixes
    if apply_defaults:
        marker = "Entered debugger"
        prefixes = ["["]

    module_map = parse_module_specs(args.binary)
    if not module_map:
        default_nt = Path("ntoskrnl/ntoskrnl.exe").resolve()
        if default_nt.exists():
            module_map["ntoskrnl.exe"] = default_nt

    symbolizer = Symbolizer(module_map)

    if args.input is None:
        try:
            lines = _run_default_capture(DEFAULT_LOG_PATH, args.timeout, args.bootmain_limit)
        except Exception as exc:  # pylint: disable=broad-except
            print(f"Failed to collect log: {exc}", file=sys.stderr)
            return 1
    elif args.input == "-":
        lines = [line.rstrip("\n") for line in sys.stdin]
    else:
        lines = _read_lines_from_path(Path(args.input))

    if not lines:
        print("Log is empty")
        return 0

    _print_tail(lines, TAIL_DEFAULT)

    filtered_lines = _filter_lines(lines, marker, prefixes)
    bt_lines = _extract_last_block(filtered_lines)
    _symbolize_block(bt_lines, symbolizer, args.show_line)

    return 0


if __name__ == "__main__":
    sys.exit(main())
