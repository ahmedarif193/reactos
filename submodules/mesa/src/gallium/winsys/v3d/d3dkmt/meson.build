# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

reactos_build_dir = get_option('reactos-build-dir')

if reactos_build_dir == ''
  error('ReactOS V3D requires -Dreactos-build-dir')
endif

vc4kmt_libdir = reactos_build_dir / 'sdk/lib/vc4kmt'
dep_vc4kmt = cc.find_library(
  'vc4kmt',
  dirs : vc4kmt_libdir,
  required : true,
  static : true,
)
dep_v3d_gdi32 = cc.find_library('gdi32', required : true)

libv3dwinsys = static_library(
  'v3dwinsys',
  files('v3d_d3dkmt_winsys.c'),
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil],
)

v3d_winsys_deps += [dep_vc4kmt, dep_v3d_gdi32]
