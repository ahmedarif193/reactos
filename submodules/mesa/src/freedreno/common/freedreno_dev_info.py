#
# Copyright © 2021 Google, Inc.
#
# SPDX-License-Identifier: MIT

from mako.template import Template
import sys
import argparse
from enum import Enum

def max_bitfield_val(high, low, shift):
    return ((1 << (high - low)) - 1) << shift

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--import-path', required=True)
parser.add_argument('--nvtop', action='store_true')
args = parser.parse_args()
sys.path.insert(0, args.import_path)

from a6xx import *


class CHIP(Enum):
    A2XX = 2
    A3XX = 3
    A4XX = 4
    A5XX = 5
    A6XX = 6
    A7XX = 7
    A8XX = 8

class CCUColorCacheFraction(Enum):
    FULL = 0
    HALF = 1
    QUARTER = 2
    EIGHTH = 3
    THREE_QUARTER = 3  # a8xx_gen2 and later


class State(object):
    def __init__(self):
        # List of unique device-info structs, multiple different GPU ids
        # can map to a single info struct in cases where the differences
        # are not sw visible, or the only differences are parameters
        # queried from the kernel (like GMEM size)
        self.gpu_infos = []

        # Table mapping GPU id to device-info struct
        self.gpus = {}

    def info_index(self, gpu_info):
        i = 0
        for info in self.gpu_infos:
            if gpu_info == info:
                return i
            i += 1
        raise Error("invalid info")

s = State()

def add_gpus(ids, info):
    for id in ids:
        s.gpus[id] = info

class GPUId(object):
    def __init__(self, gpu_id = None, chip_id = None, name=None):
        if chip_id is None:
            assert(gpu_id is not None)
            val = gpu_id
            core = int(val / 100)
            val -= (core * 100)
            major = int(val / 10)
            val -= (major * 10)
            minor = val
            chip_id = (core << 24) | (major << 16) | (minor << 8) | 0xff
        self.chip_id = chip_id
        if gpu_id is None:
            gpu_id = 0
        self.gpu_id = gpu_id
        if name is None:
            assert(gpu_id != 0)
            name = "FD%d" % gpu_id
        self.name = name

class Struct(object):
    """A helper class that stringifies itself to a 'C' struct initializer
    """
    def __str__(self):
        s = "{"
        for name, value in vars(self).items():
            s += "." + name + "=" + str(value) + ","
        return s + "}"

class GPUInfo(Struct):
    """Base class for any generation of adreno, consists of GMEM layout
       related parameters

       Note that tile_max_h is normally only constrained by corresponding
       bitfield size/shift (ie. VSC_BIN_SIZE, or similar), but tile_max_h
       tends to have lower limits, in which case a comment will describe
       the bitfield size/shift
    """
    def __init__(self, chip, gmem_align_w, gmem_align_h,
                 tile_align_w, tile_align_h,
                 tile_max_w, tile_max_h, num_vsc_pipes,
                 cs_shared_mem_size, num_sp_cores, wave_granularity, fibers_per_sp,
                 highest_bank_bit = 0, ubwc_swizzle = 0x7, macrotile_mode = 0,
                 threadsize_base = 64, max_waves = 16, compute_lb_size = 0):
        self.chip          = chip.value
        self.gmem_align_w  = gmem_align_w
        self.gmem_align_h  = gmem_align_h
        self.tile_align_w  = tile_align_w
        self.tile_align_h  = tile_align_h
        self.tile_max_w    = tile_max_w
        self.tile_max_h    = tile_max_h
        self.num_vsc_pipes = num_vsc_pipes
        self.cs_shared_mem_size = cs_shared_mem_size
        self.num_sp_cores  = num_sp_cores
        self.wave_granularity = wave_granularity
        self.fibers_per_sp = fibers_per_sp
        self.threadsize_base = threadsize_base
        self.max_waves     = max_waves
        self.highest_bank_bit = highest_bank_bit
        self.ubwc_swizzle = ubwc_swizzle
        self.macrotile_mode = macrotile_mode

        s.gpu_infos.append(self)


class A6xxGPUInfo(GPUInfo):
    """The a6xx generation has a lot more parameters, and is broken down
       into distinct sub-generations.  The template parameter avoids
       duplication of parameters that are unique to the sub-generation.
    """
    def __init__(self, chip, template, num_ccu,
                 tile_align_w, tile_align_h, tile_max_w, tile_max_h, num_vsc_pipes,
                 cs_shared_mem_size, wave_granularity, fibers_per_sp,
                 magic_regs, raw_magic_regs = None, highest_bank_bit = 15,
                 ubwc_swizzle = 0x6, macrotile_mode = 1,
                 threadsize_base = 64, max_waves = 16, num_slices = 0):
        if chip == CHIP.A6XX:
            compute_lb_size = 0
        else:
            # on a7xx the compute_lb_size is 40KB for all known parts for now.
            # We have a parameter for it in case some low-end parts cut it down.
            compute_lb_size = 40 * 1024

        super().__init__(chip, gmem_align_w = 16, gmem_align_h = 4,
                         tile_align_w = tile_align_w,
                         tile_align_h = tile_align_h,
                         tile_max_w   = tile_max_w,
                         tile_max_h   = tile_max_h,
                         num_vsc_pipes = num_vsc_pipes,
                         cs_shared_mem_size = cs_shared_mem_size,
                         num_sp_cores = num_ccu, # The # of SP cores seems to always match # of CCU
                         wave_granularity   = wave_granularity,
                         fibers_per_sp      = fibers_per_sp,
                         highest_bank_bit = highest_bank_bit,
                         ubwc_swizzle = ubwc_swizzle,
                         macrotile_mode = macrotile_mode,
                         threadsize_base    = threadsize_base,
                         max_waves    = max_waves,
                         compute_lb_size = compute_lb_size)

        self.num_ccu = num_ccu
        self.num_slices = num_slices

        self.props = Struct()

        self.magic = Struct()

        for name, val in magic_regs.items():
            setattr(self.magic, name, val)

        if raw_magic_regs:
            self.magic_raw = [[int(r[0]), r[1]] for r in raw_magic_regs]

        templates = template if isinstance(template, list) else [template]
        for template in templates:
            template.apply_props(self)


    def __str__(self):
     return super(A6xxGPUInfo, self).__str__().replace('[', '{').replace("]", "}")

class GPUProps(dict):
    unique_props = dict()
    def apply_props(self, gpu_info):
        for name, val in self.items():
            setattr(getattr(gpu_info, "props"), name, val)
            GPUProps.unique_props[(name, "props")] = val

template = """\
/* Copyright © 2021 Google, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "freedreno_dev_info.h"
#include "util/u_debug.h"
#include "util/log.h"

#include <stdlib.h>

/* Map python to C: */
#define True true
#define False false

%for info in s.gpu_infos:
static const struct fd_dev_info __info${s.info_index(info)} = ${str(info)};
%endfor

static const struct fd_dev_rec fd_dev_recs[] = {
%for id, info in s.gpus.items():
   { {${id.gpu_id}, ${hex(id.chip_id)}}, "${id.name}", &__info${s.info_index(info)} },
%endfor
};

void
fd_dev_info_apply_dbg_options(struct fd_dev_info *info)
{
    const char *env = debug_get_option("FD_DEV_FEATURES", NULL);
    if (!env || !*env)
        return;

    char *features = strdup(env);
    char *feature, *feature_end;
    feature = strtok_r(features, ":", &feature_end);
    while (feature != NULL) {
        char *name, *name_end;
        name = strtok_r(feature, "=", &name_end);

        if (!name) {
            mesa_loge("Invalid feature \\"%s\\" in FD_DEV_FEATURES", feature);
            exit(1);
        }

        char *value = strtok_r(NULL, "=", &name_end);

        feature = strtok_r(NULL, ":", &feature_end);

%for (prop, gen), val in unique_props.items():
  <%
    if isinstance(val, bool):
        parse_value = "debug_parse_bool_option"
    else:
        parse_value = "debug_parse_num_option"
  %>
        if (strcmp(name, "${prop}") == 0) {
            info->${gen}.${prop} = ${parse_value}(value, info->${gen}.${prop});
            continue;
        }
%endfor

        mesa_loge("Invalid feature \\"%s\\" in FD_DEV_FEATURES", name);
        exit(1);
    }

    free(features);
}
"""

nvtop_template="""
static const struct msm_id_struct msm_ids[] = {
%for id, info in s.gpus.items():
  { ${hex(id.chip_id)}, "${id.name}" },
%endfor
};
"""

def main():
    if args.nvtop:
        print(Template(nvtop_template).render(s=s, unique_props=GPUProps.unique_props))
    else:
        print(Template(template).render(s=s, unique_props=GPUProps.unique_props))
