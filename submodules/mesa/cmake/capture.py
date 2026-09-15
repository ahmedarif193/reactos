#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Capture a generator's stdout without shell quoting or an extra build tool."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    output = Path(sys.argv[1])
    # A failed generator must not leave an apparently up-to-date output behind.
    fd, temporary = tempfile.mkstemp(prefix=output.name + '.', dir=output.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            subprocess.run(sys.argv[2:], stdout=stream, check=True)
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    main()
