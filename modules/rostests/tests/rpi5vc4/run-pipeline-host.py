#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test the real miniport queue walker against controlled hardware completions.

Run from the source tree with a host C compiler supporting ASan and UBSan.
--revision REV exercises a historical queue walker with the same test cases.
No generated sources or executables are retained in the source tree.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def function_region(source, name, next_name):
    start = source.rfind("static ", 0, source.index(name + "("))
    end = source.rfind("static ", 0, source.index(next_name + "("))
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="Git revision of the miniport to test")
    parser.add_argument("--source-root", type=Path, help="ReactOS source directory")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = args.source_root or here.parents[3]
    relative_source = "drivers/directx/rpi5vc4/rpi5vc4_wddm.c"
    if args.revision:
        source = subprocess.check_output(
            ["git", "show", f"{args.revision}:{relative_source}"], cwd=root, text=True
        )
    else:
        source = (root / relative_source).read_text()
    header = (root / "drivers/directx/rpi5vc4/rpi5vc4.h").read_text()
    declaration = "typedef struct _RPI5VC4_PENDING_SUBMIT"
    terminator = "} RPI5VC4_PENDING_SUBMIT, *PRPI5VC4_PENDING_SUBMIT;"
    start = header.index(declaration)
    end = header.index(terminator, start) + len(terminator)

    with tempfile.TemporaryDirectory(prefix="rpi5-pipeline-") as directory:
        work = Path(directory)
        (work / "pending.h").write_text(header[start:end])
        (work / "helpers.h").write_text("\n".join(
            function_region(source, name, next_name)
            for name, next_name in (
                ("Rpi5Vc4GpuJobActiveLocked", "Rpi5Vc4SelectAddressSpaceLocked"),
                ("Rpi5Vc4OldestQueuedProcessLocked", "Rpi5Vc4KickBinLocked"),
                ("Rpi5Vc4OverlapCandidate", "Rpi5Vc4ActiveBinnerLocked"),
            )
        ))
        start = source.rfind("static ", 0, source.index("Rpi5Vc4ProcessPendingLocked("))
        end = source.index("\n/*\n * Queue the single completion-drain owner.", start)
        (work / "pipeline.h").write_text(source[start:end])
        executable = work / "pipeline-test"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + [
            "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(work), str(here / "pipeline_host.c"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
