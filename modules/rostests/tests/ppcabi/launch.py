#!/usr/bin/env python3
"""Boot a prepared NT4 maciNTosh/Gossamer test copy using dingusppc-nt."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--arc-iso', required=True, type=Path)
    parser.add_argument('--prepared', required=True, type=Path)
    parser.add_argument('--machine', default='pmg3dt')
    args = parser.parse_args()
    out = args.prepared.resolve()
    manifest = json.loads((out / 'preparation.json').read_text())
    disk = out / 'disk01.img'
    if disk.resolve() == Path(manifest['source']).resolve():
        parser.error('the prepared disk must differ from the original')
    for path in [args.emulator, args.rom, args.arc_iso, disk]:
        if not path.is_file():
            parser.error('missing input: ' + str(path))
    command = [str(args.emulator.resolve()), '--machine', args.machine,
               '--bootrom', str(args.rom.resolve()), '--rambank1_size', '128',
               '--hdd_img', str(disk), '--cdr_img', str(args.arc_iso.resolve())]
    (out / 'launch.json').write_text(json.dumps({
        'command': command, 'cwd': str(out),
        'rom_sha256': hashlib.sha256(args.rom.read_bytes()).hexdigest(),
        'firmware_sha256': hashlib.sha256(args.arc_iso.read_bytes()).hexdigest(),
    }, indent=2) + '\n')
    print('Booting the test copy; results are in C:\\ppcabi.', flush=True)
    return subprocess.call(command, cwd=out)


if __name__ == '__main__':
    raise SystemExit(main())
