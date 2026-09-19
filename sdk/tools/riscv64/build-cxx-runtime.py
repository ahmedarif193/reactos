#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ahmed ARIF
"""Build matching Windows/RISC-V LLVM runtimes against the ReactOS SDK.

Configure ReactOS first. Supply the LLVM source checkout matching its compiler.
No host C/C++ headers or libraries participate in this build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def cmake_string(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-source', required=True, type=Path)
    parser.add_argument('--reactos-build', required=True, type=Path)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--jobs', default=8, type=int)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[3]
    llvm = args.llvm_source.resolve()
    build = args.reactos_build.resolve()
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    if cache.get('ARCH') != 'riscv64':
        parser.error('--reactos-build must be a configured RISC-V build')
    toolchain = (args.toolchain or Path(cache['REACTOS_CLANG_LLVM_MINGW_ROOT'])).resolve()
    prefix = (args.prefix or build / 'riscv64-cxx-runtime').resolve()
    runtime = build / 'riscv64-cxx-build'
    runtime.mkdir(parents=True, exist_ok=True)
    clang = toolchain / 'bin/clang'
    resource = Path(subprocess.check_output([str(clang), '-print-resource-dir'], text=True).strip())
    for path in [llvm / 'runtimes/CMakeLists.txt', llvm / 'libcxxabi/include/cxxabi.h',
                 llvm / 'libunwind/src/Unwind-seh.cpp']:
        if not path.is_file():
            parser.error(f'missing matching runtime input: {path}')
    run('cmake', '--build', build, '--target', 'xdk')
    includes = [resource / 'include'] + [source / 'sdk/include' / path for path in
               ['ucrt', 'vcruntime', 'crt', 'psdk', '', 'ddk', 'reactos']] + [
               build / 'sdk/include', build / 'sdk/include/psdk', build / 'sdk/include/ddk']
    flags = ('-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax -funwind-tables '
             '-D_DLL -D__USE_CRTIMP -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 '
             '-D__REACTOS__ -D_CRT_DECLARE_NONSTDC_NAMES=1 -D__LARGE_MBSTATE_T '
             '-nostdlibinc ')
    flags += ' '.join('-isystem ' + cmake_string(path) for path in includes)
    config = ['set(CMAKE_SYSTEM_NAME Windows)', 'set(CMAKE_SYSTEM_PROCESSOR riscv64)',
              'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)']
    for language, compiler in [('C', 'clang'), ('CXX', 'clang++'), ('ASM', 'clang')]:
        config += [f'set(CMAKE_{language}_COMPILER {cmake_string(toolchain / "bin" / compiler)})',
                   f'set(CMAKE_{language}_COMPILER_TARGET riscv64-w64-windows-gnu)']
    for name, executable in [('AR', 'llvm-ar'), ('RANLIB', 'llvm-ranlib')]:
        config += [f'set(CMAKE_{name} {cmake_string(toolchain / "bin" / executable)})']
    for language in ['C', 'CXX']:
        config += [f'set(CMAKE_{language}_FLAGS_INIT {cmake_string(flags)})',
                   f'set(CMAKE_{language}_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)']
    toolchain_file = runtime / 'reactos-toolchain.cmake'
    toolchain_file.write_text('\n'.join(config) + '\n')
    options = {
        'CMAKE_TOOLCHAIN_FILE': toolchain_file,
        'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_INSTALL_PREFIX': prefix,
        'LLVM_ENABLE_RUNTIMES': 'libunwind;libcxxabi;libcxx', 'LIBCXX_CXX_ABI': 'libcxxabi',
        'LIBUNWIND_ENABLE_SHARED': 'OFF', 'LIBUNWIND_ENABLE_STATIC': 'ON',
        'LIBUNWIND_ENABLE_THREADS': 'ON', 'LIBUNWIND_INCLUDE_TESTS': 'OFF',
        'LIBUNWIND_USE_COMPILER_RT': 'ON',
        'LIBCXXABI_ENABLE_SHARED': 'OFF', 'LIBCXXABI_ENABLE_STATIC': 'ON',
        'LIBCXXABI_USE_LLVM_UNWINDER': 'ON', 'LIBCXXABI_INCLUDE_TESTS': 'OFF',
        'LIBCXXABI_USE_COMPILER_RT': 'ON',
        'LIBCXX_ENABLE_SHARED': 'OFF', 'LIBCXX_ENABLE_STATIC': 'ON',
        'LIBCXX_ENABLE_STATIC_ABI_LIBRARY': 'OFF', 'LIBCXX_ENABLE_ABI_LINKER_SCRIPT': 'OFF',
        'LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS': 'ON',
        'LIBCXX_ENABLE_TESTS': 'OFF', 'LIBCXX_INCLUDE_TESTS': 'OFF',
        'LIBCXX_INCLUDE_BENCHMARKS': 'OFF', 'LIBCXX_HAS_WIN32_THREAD_API': 'ON',
        'LIBCXX_HAS_PTHREAD_API': 'OFF', 'LIBCXX_HAS_ATOMIC_LIB': 'OFF',
        'LIBCXX_ENABLE_FILESYSTEM': 'ON', 'LIBCXX_ENABLE_EXPERIMENTAL_LIBRARY': 'OFF',
    }
    # Recreate compiler detection: CMake otherwise retains a previous compiler
    # even when the toolchain file points to a newly patched LLVM package.
    run('cmake', '--fresh', '-S', llvm / 'runtimes', '-B', runtime, '-G', 'Ninja',
        *(f'-D{key}={value}' for key, value in options.items()))
    run('cmake', '--build', runtime, '--target', 'cxx', 'cxxabi', 'unwind', '--parallel', args.jobs)
    run('cmake', '--install', runtime)
    builtins = build / 'riscv64-builtins-build'
    run('cmake', '--fresh', '-S', llvm / 'compiler-rt/lib/builtins', '-B', builtins,
        '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain_file}',
        '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}',
        '-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON', '-DCOMPILER_RT_INCLUDE_TESTS=OFF',
        '-DCOMPILER_RT_BUILD_CRT=OFF', '-DCOMPILER_RT_BUILTINS_ENABLE_PIC=OFF')
    run('cmake', '--build', builtins, '--parallel', args.jobs)
    run('cmake', '--install', builtins)
    manifest = {'compiler': subprocess.check_output([str(clang), '--version'], text=True),
                'llvm_source': str(llvm), 'flags': flags, 'inputs': {}}
    for path in [prefix / 'lib/libc++abi.a', prefix / 'lib/libunwind.a', prefix / 'lib/libc++.a',
                 prefix / 'lib/windows/libclang_rt.builtins-riscv64.a']:
        manifest['inputs'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    (prefix / 'reactos-runtime.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
