#!/usr/bin/env python3
"""Copy a FAT-based NT4 PPC disk and install the ABI boot service on the copy."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image', required=True, type=Path)
    p.add_argument('--tests', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    args = p.parse_args()
    original, tests, out = (x.resolve() for x in (args.image, args.tests, args.output))
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'disk01.img'
    if disk.exists():
        p.error('output disk already exists; choose a new output directory')
    with original.open('rb') as f:
        mbr = f.read(512)
    if mbr[510:512] != b'\x55\xaa':
        p.error('input has no MBR signature')
    # This harness deliberately supports only the supplied FAT installation.
    entry = mbr[462:478]
    if entry[4] not in (4, 6, 14):
        p.error('partition 2 must be the NT4 FAT16 system volume')
    start, count = struct.unpack_from('<II', entry, 8)
    if not start or (start + count) * 512 > original.stat().st_size:
        p.error('invalid system partition bounds')
    for name in ['abi-o0.exe', 'abi-o2.exe', 'runner.exe', 'build.json']:
        if not (tests / name).is_file():
            p.error('missing probe: ' + name)
    before = digest(original)
    run('cp', '--reflink=auto', '--sparse=always', original, disk)
    volume = str(disk) + '@@' + str(start * 512)
    hive = out / 'SYSTEM.abi'
    run('mcopy', '-i', volume, '::/WINNT/system32/config/system', hive)
    (out / 'SYSTEM.original').write_bytes(hive.read_bytes())
    run('mmd', '-i', volume, '::/ppcabi')
    for name in ['abi-o0.exe', 'abi-o2.exe', 'runner.exe', 'run.cmd']:
        run('mcopy', '-i', volume, tests / name, '::/ppcabi/' + name)
    # Add only the test service. Do not change logon, passwords or OS binaries.
    reg = out / 'service.reg'
    service = ('"Type"=dword:00000010\n"Start"=dword:00000002\n'
               '"ErrorControl"=dword:00000001\n'
               '"ImagePath"="C:\\\\ppcabi\\\\runner.exe"\n'
               '"DisplayName"="PowerPC ABI reference tests"\n'
               '"ObjectName"="LocalSystem"\n')
    reg.write_text('Windows Registry Editor Version 5.00\n\n' + '\n'.join(
        '[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet%03d\\Services\\PpcAbiProbe]\n' % n + service
        for n in (1, 3)))
    with (out / 'registry-import.log').open('w') as log:
        # reged returns 2 after successfully saving a changed hive, and can
        # return 0 for a rejected input. Verify the exported key below.
        imported = subprocess.run(['reged', '-C', '-I', str(hive),
                                   'HKEY_LOCAL_MACHINE\\SYSTEM', str(reg)],
                                  stdout=log, stderr=subprocess.STDOUT)
        if imported.returncode not in (0, 2):
            raise RuntimeError('registry editor failed')
    # Export the exact new key as evidence before writing the copied hive back.
    run('reged', '-x', hive, 'HKEY_LOCAL_MACHINE\\SYSTEM',
        '\\ControlSet001\\Services\\PpcAbiProbe', out / 'service-verified.reg',
        stdout=subprocess.DEVNULL)
    verified = (out / 'service-verified.reg').read_text()
    if 'PpcAbiProbe' not in verified or 'runner.exe' not in verified:
        raise RuntimeError('service registry import did not create the expected key')
    run('mcopy', '-o', '-i', volume, hive, '::/WINNT/system32/config/system')
    after = digest(original)
    if before != after:
        raise RuntimeError('source image changed during preparation')
    (out / 'preparation.json').write_text(json.dumps({
        'source': str(original), 'source_sha256': before,
        'source_sha256_after': after, 'copy': str(disk),
        'system_partition_offset': start * 512,
        'binaries': {n: digest(tests / n) for n in
                     ['abi-o0.exe', 'abi-o2.exe', 'runner.exe']},
        'results': 'C:\\ppcabi\\runner.log and abi-o[02].<case>.log',
    }, indent=2) + '\n')
    print('Prepared', disk)


if __name__ == '__main__':
    main()
