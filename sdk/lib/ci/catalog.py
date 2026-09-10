#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 Ahmed ARIF
"""Generate a content-based OS image catalog for the kernel's read-only data."""

import hashlib
import pathlib
import sys


def main():
    manifest, output = map(pathlib.Path, sys.argv[1:])
    entries = set()
    for name in manifest.read_text().splitlines():
        path = pathlib.Path(name)
        data = path.read_bytes()
        if data[:2] != b"MZ":
            raise ValueError(f"System image is not a PE file: {path}")
        entries.add((len(data), hashlib.sha256(data).digest()))
    if not entries:
        raise ValueError("The system image catalog must not be empty")
    lines = ["/* Generated from this build's system images. Do not edit. */",
             "static const struct { ULONGLONG Size; UCHAR Hash[32]; } MiSystemImageCatalog[] = {"]
    for size, digest in sorted(entries):
        values = ", ".join(f"0x{byte:02x}" for byte in digest)
        lines.append(f"    {{{size}ULL, {{{values}}}}},")
    lines.append("};\n")
    content = "\n".join(lines)
    if not output.exists() or output.read_text() != content:
        temporary = output.with_suffix(".tmp")
        temporary.write_text(content)
        temporary.replace(output)


if __name__ == "__main__":
    main()
