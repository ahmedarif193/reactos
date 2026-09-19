#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production USBPORT DMA mapper with simulated HAL mappings."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--revision', help='Git revision of usbport.c to test')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[3]
name = 'drivers/usb/usbport/usbport.c'
source = (subprocess.check_output(['git', 'show', f'{args.revision}:{name}'], cwd=root, text=True)
          if args.revision else (root/name).read_text())
start = source.index('IO_ALLOCATION_ACTION\nNTAPI\nUSBPORT_MapTransfer(')
end = source.index('\nstatic\nNTSTATUS\nUSBPORT_SubmitMapTransfer(', start)
with tempfile.TemporaryDirectory(prefix='usbport-map-') as directory:
    work = Path(directory)
    (work/'map_transfer.h').write_text(source[start:end])
    executable = work/'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(work), str(here/'map_transfer_host.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
