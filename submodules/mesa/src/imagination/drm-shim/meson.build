# Copyright © 2026 Imagination Technologies Ltd.
# Copyright 2019 Broadcom
# SPDX-License-Identifier: MIT

libpowervr_noop_drm_shim = shared_library(
  'powervr_noop_drm_shim',
  'pvr_noop.c',
  link_with : [libpowervr_common],
  include_directories: [inc_imagination, inc_include, inc_src],
  dependencies: [dep_drm_shim, dep_valgrind, dep_csbgen],
  gnu_symbol_visibility : 'hidden',
  install : true,
)
