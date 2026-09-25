#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Forward the invoking Ninja/CMake job count to a nested cmake --build.

Ninja does not export its -j setting. Read only the ancestor process chain at
build time so changing -j requires neither a cache option nor reconfiguration.
No external Python modules are required.

Ninja withholds a command's output until the command exits, so a nested build
is invisible while it runs. Mirror its progress to the controlling terminal as
it happens and keep the captured log free of the duplicated progress lines.
"""
import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


NESTED_BUILD_MIRROR = 'REACTOS_NESTED_BUILD_MIRROR'
PROGRESS_LINE = re.compile(rb'^\[[0-9]+/[0-9]+\]')
CONSOLE_ERASE = b'\r\x1b[K'
if os.name == 'nt' or os.environ.get('TERM', 'dumb') == 'dumb':
    CONSOLE_ERASE = b'\r'
MIRROR_INTERVAL = 0.5


def job_count(argv):
    """Return an explicit job count, respecting option arguments and last -j."""
    if not argv:
        return None
    program = Path(argv[0]).name.lower()
    if program.endswith('.exe'):
        program = program[:-4]
    if program not in ('ninja', 'ninja-build', 'cmake'):
        return None
    if program == 'cmake' and '--build' not in argv:
        return None
    jobs = None
    args = iter(argv[1:])
    for arg in args:
        if arg in ('--', '-t'):
            break
        if arg in ('-C', '-f', '-l', '-k', '-d', '-w', '--build', '--target', '--config'):
            next(args, None)
            continue
        if arg in ('-j', '--parallel'):
            value = next(args, '')
        elif re.fullmatch(r'-j[0-9]+', arg):
            value = arg[2:]
        elif arg.startswith('--parallel=') and program == 'cmake':
            value = arg.partition('=')[2]
        else:
            continue
        if value.isdecimal():
            jobs = int(value)
    return jobs


def darwin_argv(pid):
    # CTL_KERN / KERN_PROCARGS2: argc, executable path, padding, argv, environ.
    # Decode exactly argc arguments, never the environment following them.
    libc = ctypes.CDLL(None, use_errno=True)
    sysctl = libc.sysctl
    sysctl.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint,
                      ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                      ctypes.c_void_p, ctypes.c_size_t]
    mib = (ctypes.c_int * 3)(1, 49, pid)
    size = ctypes.c_size_t()
    if sysctl(mib, 3, None, ctypes.byref(size), None, 0):
        raise OSError(ctypes.get_errno(), 'Cannot read ancestor arguments')
    data = ctypes.create_string_buffer(size.value)
    if sysctl(mib, 3, data, ctypes.byref(size), None, 0):
        raise OSError(ctypes.get_errno(), 'Cannot read ancestor arguments')
    raw = data.raw[:size.value]
    argc = int.from_bytes(raw[:4], sys.byteorder)
    argv = raw[raw.index(b'\0', 4) + 1:].lstrip(b'\0').split(b'\0')[:argc]
    return [os.fsdecode(arg) for arg in argv]


def windows_ancestors(pid):
    # Query only ancestors, without requiring psutil or deprecated wmic.exe.
    script = '''$idToRead = %d; $seen = @{}; $items = @();
while ($idToRead -gt 0 -and -not $seen.ContainsKey($idToRead)) {
  $seen[$idToRead] = $true;
  $p = Get-CimInstance Win32_Process -Filter "ProcessId=$idToRead";
  if (-not $p) { break };
  $items += [string]$p.CommandLine;
  $idToRead = [int]$p.ParentProcessId;
}
ConvertTo-Json -InputObject $items -Compress
''' % pid
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive',
                             '-Command', script], check=True, capture_output=True,
                            text=True, timeout=15)
    shell32 = ctypes.WinDLL('shell32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    shell32.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell32.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    for command in json.loads(result.stdout):
        if not command:
            continue
        argc = ctypes.c_int()
        argv = shell32.CommandLineToArgvW(command, ctypes.byref(argc))
        if not argv:
            raise OSError('Cannot split ancestor command line')
        try:
            yield [argv[index] for index in range(argc.value)]
        finally:
            kernel32.LocalFree(argv)


def ancestors():
    pid = os.getppid()
    try:
        if os.name == 'nt':
            yield from windows_ancestors(pid)
            return
        seen = set()
        while pid > 1 and pid not in seen:
            seen.add(pid)
            if sys.platform == 'darwin':
                argv = darwin_argv(pid)
                parent = subprocess.check_output(
                    ['ps', '-p', str(pid), '-o', 'ppid='], text=True)
                pid = int(parent.strip())
            else:
                proc = Path('/proc') / str(pid)
                argv = [os.fsdecode(arg) for arg in (proc / 'cmdline').read_bytes().split(b'\0')[:-1]]
                pid = int((proc / 'stat').read_text().rsplit(')', 1)[1].split()[1])
            yield argv
    except (OSError, ValueError, subprocess.SubprocessError):
        # Process information may be unavailable on a restricted build host.
        # CMAKE_BUILD_PARALLEL_LEVEL remains a portable fallback.
        return


def build_command(command, jobs, environment):
    command = list(command)
    environment = dict(environment)
    if jobs is None:
        return command, environment
    if jobs == 0:
        # CMake requires a positive --parallel count; Ninja spells unlimited -j0.
        environment.pop('CMAKE_BUILD_PARALLEL_LEVEL', None)
        if '--' not in command:
            command.append('--')
        command.append('-j0')
    else:
        environment['CMAKE_BUILD_PARALLEL_LEVEL'] = str(jobs)
        if job_count(command) is None:
            index = command.index('--') if '--' in command else len(command)
            command[index:index] = ['--parallel', str(jobs)]
    return command, environment


def open_console():
    """Return the controlling terminal when Ninja is capturing our output."""
    if os.environ.get(NESTED_BUILD_MIRROR):
        return None
    try:
        if sys.stdout.isatty():
            return None
    except (AttributeError, ValueError):
        return None
    try:
        return open('CONOUT$' if os.name == 'nt' else '/dev/tty', 'wb', buffering=0)
    except OSError:
        return None


def show(console, prefix, line):
    """Write one tagged line over whatever status line Ninja last drew."""
    try:
        console.write(CONSOLE_ERASE + prefix + line + b'\n')
    except OSError:
        pass


def mirror_build(command, environment, console):
    """Run the nested build, streaming its progress to the terminal."""
    environment = dict(environment)
    environment[NESTED_BUILD_MIRROR] = '1'
    name = Path(command[command.index('--build') + 1]).resolve().name.lstrip('_')
    prefix = b'[' + (name or 'nested').encode() + b'] '
    process = subprocess.Popen(command, env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    captured = []
    pending = None
    shown = 0.0
    for line in iter(process.stdout.readline, b''):
        captured.append(line)
        line = line.rstrip()
        if PROGRESS_LINE.match(line):
            pending = line
            if time.monotonic() - shown < MIRROR_INTERVAL:
                continue
            show(console, prefix, pending)
            pending = None
            shown = time.monotonic()
            continue
        if not line:
            continue
        if pending is not None:
            show(console, prefix, pending)
            pending = None
        show(console, prefix, line)
        shown = time.monotonic()
    if pending is not None:
        show(console, prefix, pending)
    process.stdout.close()
    status = process.wait()
    if not status:
        captured = [line for line in captured if not PROGRESS_LINE.match(line)]
    sys.stdout.buffer.writelines(captured)
    sys.stdout.buffer.flush()
    return status


def main():
    if len(sys.argv) < 4 or sys.argv[2] != '--build':
        raise SystemExit('Usage: build-with-parallel.py <cmake> --build <directory> [build options]')
    command = sys.argv[1:]
    jobs = job_count(command)
    if jobs is None:
        jobs = next((count for argv in ancestors()
                     if (count := job_count(argv)) is not None), None)
    command, environment = build_command(command, jobs, os.environ)
    console = open_console()
    try:
        if console is None:
            return subprocess.call(command, env=environment)
        with console:
            return mirror_build(command, environment, console)
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
