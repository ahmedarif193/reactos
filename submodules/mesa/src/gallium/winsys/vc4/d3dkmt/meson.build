# Copyright 2026 Ahmed Arif <arif193@gmail.com>
# SPDX-License-Identifier: MIT

reactos_build_dir = get_option('reactos-build-dir')

if reactos_build_dir == ''
  error('ReactOS VC4 requires -Dreactos-build-dir')
endif

rpi3vc4kmt_libdir = reactos_build_dir / 'sdk/lib/rpi3vc4kmt'
dep_rpi3vc4kmt = cc.find_library(
  'rpi3vc4kmt',
  dirs : rpi3vc4kmt_libdir,
  required : true,
  static : true,
)
dep_vc4_gdi32 = cc.find_library('gdi32', required : true)
dep_vc4_trace = declare_dependency(include_directories : include_directories(reactos_source_dir / 'sdk/include/reactos'))

libvc4winsys = static_library(
  'vc4winsys',
  files('vc4_d3dkmt_winsys.c'),
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, dep_vc4_trace],
)

vc4_winsys_deps += [dep_rpi3vc4kmt, dep_vc4_gdi32, dep_vc4_trace]
