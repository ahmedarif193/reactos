#!/usr/bin/env python3
COPYRIGHT = """\
/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
"""

import argparse
import os
import sys

from mako.template import Template
from mako import exceptions

sys.path.append(f"{os.path.dirname(sys.argv[0])}/../dev")
import intel_device_info

template = COPYRIGHT + """

/* DO NOT EDIT - This file generated automatically by intel_device_serialize_c.py script */

#include "dev/intel_device_info.h"
#include "brw_compiler.h"
#define SHA_UPDATE_FIELD(field)     _mesa_blake3_update(ctx, &devinfo->field, sizeof(devinfo->field))

void
brw_device_blake3_update(blake3_hasher *ctx,
                       const struct intel_device_info *devinfo) {
% for member in compiler_fields:
% if member.ray_tracing_field:
   if (devinfo->has_ray_tracing)
      SHA_UPDATE_FIELD(${member.name});
% else:
   SHA_UPDATE_FIELD(${member.name});
% endif
% endfor
}

#undef SHA_UPDATE_FIELD

"""

def main():
    """print intel_device_serialize.c at the specified path"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True,
                        help='Output C file')
    args = parser.parse_args()
    device_members = intel_device_info.TYPES_BY_NAME["intel_device_info"].members
    compiler_fields = [field for field in device_members if field.compiler_field]
    with open(args.out, 'w', encoding='utf-8') as f:
        try:
            f.write(Template(template).render(compiler_fields=compiler_fields))
        except:
            print(exceptions.text_error_template().render(compiler_fields=compiler_fields))

if __name__ == "__main__":
    main()
