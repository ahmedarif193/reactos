# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

libzinkwinsys = static_library(
  'zinkwinsys',
  files('zink_drm_winsys.c'),
  include_directories : [
    inc_gallium_aux, inc_gallium_drivers,
  ],
  dependencies : idep_mesautil,
  gnu_symbol_visibility : 'hidden',
)
