#!/usr/bin/env python3
"""Require complete probe logs from the expected operating system version."""
import argparse
from pathlib import Path
import re

from cases import CASES


def process_results(directory, expected):
    """Check process termination as well as the probes' own completion logs."""
    service = directory / 'runner.log'
    path = service if service.is_file() else directory / 'batch.log'
    if not path.is_file():
        return ['missing runner.log or batch.log with process exit codes']
    lines = [line.strip() for line in path.read_text(errors='replace').splitlines()
             if line.strip()]
    errors, completed = [], []
    if not lines or lines[-1] != 'COMPLETE':
        errors.append(f'{path.name}: runner did not complete')
    if path == service:
        pending = None
        for line in lines:
            start = re.fullmatch(r'START .*abi-(o[02])\.exe (\w+)', line)
            result = re.fullmatch(r'EXIT (0x[0-9a-fA-F]+)', line)
            if start:
                if pending is not None:
                    errors.append(f'{path.name}: no exit code for {pending}')
                pending = (start[1], start[2])
            elif result:
                if pending is None or int(result[1], 16):
                    errors.append(f'{path.name}: unexpected or failed exit: {line}')
                completed.append(pending)
                pending = None
            elif 'FAILED' in line:
                errors.append(f'{path.name}: {line}')
        if pending is not None:
            errors.append(f'{path.name}: no exit code for {pending}')
    else:
        for line in lines:
            result = re.fullmatch(r'(o[02]) (\w+) exit (-?\d+)', line)
            if result:
                completed.append((result[1], result[2]))
                if int(result[3]):
                    errors.append(f'{path.name}: {line}')
    if completed != expected:
        errors.append(f'{path.name}: process results are missing, duplicated or out of order')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--os-major', type=int, default=4,
                        help='expected OS major version (default: 4 for NT4)')
    parser.add_argument('--legacy-20', action='store_true',
                        help='validate the archived 20-process suite before NtContinue was added')
    args = parser.parse_args()
    cases = tuple(case for case in CASES if case != 'ntcontinue') if args.legacy_20 else CASES
    expected = [(optimization, case) for optimization in ['o0', 'o2'] for case in cases]
    errors, checks = process_results(args.results, expected), 0
    for optimization in ['o0', 'o2']:
        for case in cases:
            path = args.results / f'abi-{optimization}.{case}.log'
            if not path.is_file():
                errors.append(f'{path.name}: missing')
                continue
            text = path.read_text(errors='replace')
            version = re.search(r'OS major=(0x[0-9a-f]+) minor=(0x[0-9a-f]+)', text)
            complete = re.search(r'COMPLETE checks=(0x[0-9a-f]+) failures=(0x[0-9a-f]+)', text)
            if not version or (int(version[1], 16), int(version[2], 16)) != (args.os_major, 0):
                errors.append(f'{path.name}: wrong or missing OS version')
            if not complete:
                errors.append(f'{path.name}: did not complete')
            else:
                checks += int(complete[1], 16)
                minimum = {'jump': 4, 'jumpseh': 5, 'ntdll': 19}.get(case, 1)
                if int(complete[1], 16) < minimum or int(complete[2], 16):
                    errors.append(f'{path.name}: failed or empty test')
            if f'START {case}\n' not in text or 'FAIL ' in text or 'UNHANDLED' in text:
                errors.append(f'{path.name}: case marker missing or failure recorded')
    for error in errors:
        print(error)
    print(f'{checks} checks; {len(errors)} result validation errors; expected OS {args.os_major}.0')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
