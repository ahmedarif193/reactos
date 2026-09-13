#encoding=utf-8

# Copyright (C) 2021 Collabora, Ltd.
# SPDX-License-Identifier: MIT

import argparse
import sys
from valhall import valhall_parse_isa

parser = argparse.ArgumentParser()
parser.add_argument('--xml', required=True, help='Input ISA XML file')

args = parser.parse_args()

(_, _, enums, _, safe_name) = valhall_parse_isa(args.xml)

print("#ifndef __VALHALL_ENUMS_H_")
print("#define __VALHALL_ENUMS_H_")

for enum in sorted(enums):
    print(f"enum va_{safe_name(enum)} {{")

    for i, value in enumerate(enums[enum].values):
        if value.value != 'reserved':
            key = safe_name(f"va_{enum}_{value.value}")
            print(f"   {key.upper()} = {i},")

    print("};\n")

print("#endif")
