#!/usr/bin/env python3
"""Build standalone NT4-compatible PPC probes with the configured compiler.

Uses SDK headers and newly generated import libraries only. No ReactOS DLL,
startup, exception dispatcher, CRT, or other runtime object is linked.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from cases import CASES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reactos-build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[4]
    build = args.reactos_build.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if cache.get('ARCH') != 'ppc':
        parser.error('a configured PowerPC ReactOS build is required')
    tool = Path(cache['REACTOS_CLANG_LLVM_MINGW_ROOT']) / 'bin'
    commands = []

    def run(*command):
        commands.append([str(arg) for arg in command])
        subprocess.run(commands[-1], check=True)

    imports = {
        'kernel32': ['CreateFileA', 'WriteFile', 'FlushFileBuffers', 'CloseHandle',
                     'OutputDebugStringA', 'ExitProcess', 'RaiseException',
                     'CreateThread', 'WaitForSingleObject', 'GetExitCodeThread',
                     'LoadLibraryA', 'GetModuleHandleA', 'GetProcAddress',
                     'FreeLibrary', 'lstrcmpA', 'GetVersionExA', 'SetErrorMode',
                     'SetUnhandledExceptionFilter', 'CreateProcessA',
                     'GetLastError', 'GetExitCodeProcess', 'TerminateProcess',
                     'GetCommandLineA'],
        'ntdll': ['__C_specific_handler', 'NtContinue', 'RtlCaptureContext',
                  'RtlLookupFunctionEntry', 'RtlVirtualUnwind'],
        'msvcrt': ['setjmp', '_setjmpex', 'longjmp'],
        'advapi32': ['RegisterServiceCtrlHandlerA', 'SetServiceStatus',
                     'StartServiceCtrlDispatcherA'],
    }
    for dll, names in imports.items():
        definition = out / (dll + '.def')
        definition.write_text('LIBRARY ' + dll + '.dll\nEXPORTS\n' +
                              '\n'.join(names) + '\n')
        run(tool / 'llvm-dlltool', '-m', 'ppc', '-d', definition,
            '-l', out / (dll + '.a'))
    includes = [source / 'sdk/include' / p for p in
                ['', 'crt', 'psdk', 'ddk', 'ndk', 'reactos', 'vcruntime']]
    includes += [build / 'sdk/include', build / 'sdk/include/psdk',
                 build / 'sdk/include/ddk']
    flags = ['--target=powerpcle-w64-windows-gnu', '-mcpu=604',
             '-D_PPC_', '-D_WIN32_WINNT=0x0400', '-DWINVER=0x0400',
             '-D__REACTOS__', '-D_USE_NATIVE_SEH=1', '-ffreestanding',
             '-fno-builtin', '-fms-extensions', '-fexceptions',
             '-funwind-tables', '-Xclang', '-fasync-exceptions', '-nostdlibinc',
             '-Wall', '-Wextra', '-Wno-pragma-pack', '-Wno-ignored-attributes']
    for p in includes:
        flags += ['-I', str(p)]
    for optimization in ['O0', 'O2']:
        name = 'abi-' + optimization.lower()
        obj = out / (name + '.obj')
        run(tool / 'clang', *flags, '-' + optimization,
            '-DABI_LOG="' + name + '.log"', '-c', Path(__file__).with_name('ppcabi.c'),
            '-o', obj)
        jump_obj = out / (name + '-jump.obj')
        run(tool / 'clang', *(flag for flag in flags
                              if flag not in ('-fno-builtin', '-ffreestanding')),
            '-' + optimization, '-c', Path(__file__).with_name('jump.c'),
            '-o', jump_obj)
        jumpseh_obj = out / (name + '-jumpseh.obj')
        run(tool / 'clang', *(flag for flag in flags
                              if flag not in ('-fno-builtin', '-ffreestanding')),
            '-' + optimization, '-c', Path(__file__).with_name('jumpseh.c'),
            '-o', jumpseh_obj)
        run(tool / 'lld-link', '/machine:ppc', '/subsystem:console,4.0',
            '/osversion:4.0', '/entry:test_entry', '/nodefaultlib',
            '/base:0x400000', '/dynamicbase:no', '/nxcompat:no',
            '/out:' + str(out / (name + '.exe')), obj, jump_obj, jumpseh_obj,
            out / 'kernel32.a', out / 'ntdll.a', out / 'msvcrt.a')
    run(tool / 'clang', *flags, '-O2', '-c', Path(__file__).with_name('runner.c'),
        '-o', out / 'runner.obj')
    run(tool / 'lld-link', '/machine:ppc', '/subsystem:console,4.0',
        '/osversion:4.0', '/entry:service_entry', '/nodefaultlib',
        '/base:0x400000', '/dynamicbase:no', '/nxcompat:no',
        '/out:' + str(out / 'runner.exe'), out / 'runner.obj',
        out / 'kernel32.a', out / 'advapi32.a')
    manifest = {'compiler': subprocess.check_output([str(tool / 'clang'), '--version'],
                                                   text=True),
                'commands': commands,
                'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in out.glob('*.exe')}}
    (out / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    batch = ['@echo off', 'cd /d C:\\ppcabi', 'echo START > batch.log']
    for optimization in ['o0', 'o2']:
        for case in CASES:
            batch += [f'abi-{optimization}.exe {case}',
                      f'echo {optimization} {case} exit %errorlevel% >> batch.log']
    batch += ['echo COMPLETE >> batch.log']
    (out / 'run.cmd').write_bytes(('\r\n'.join(batch) + '\r\n').encode())
    print('Built NT4 PPC probes in', out)


if __name__ == '__main__':
    main()
