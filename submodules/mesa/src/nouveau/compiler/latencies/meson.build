# Copyright © 2025 Collabora, Ltd.
# SPDX-License-Identifier: MIT

_nak_lat_sm100_files = [
  'pred_raw.csv',
  'pred_war.csv',
  'pred_waw.csv',
  'reg_raw.csv',
  'reg_war.csv',
  'reg_waw.csv',
  'upred_raw.csv',
  'upred_war.csv',
  'upred_waw.csv',
  'ureg_raw.csv',
  'ureg_war.csv',
  'ureg_waw.csv',
]

_nak_lat_sms = {
  'sm100': _nak_lat_sm100_files,
}

_lat_rs_gen = files('lat_rs_gen.py')

_lat_rs_generated = []
foreach sm, csvs : _nak_lat_sms
  csv_files = []
  foreach i : range(csvs.length())
    csv_files += files(sm + '/' + csvs[i])
  endforeach

  _lat_rs_generated += custom_target(
    sm+'.rs',
    input : [_lat_rs_gen, csv_files],
    output : [sm+'.rs'],
    command : [
      prog_python, '@INPUT@', '-p', nouveau_util_py_path,
      '--out-rs', '@OUTPUT0@', '--sm', sm,
    ],
    depend_files : nouveau_util_py,
  )
endforeach

_nak_latencies_lib_rs = custom_target(
  'lib.rs',
  input : ['lib_rs_gen.py', _lat_rs_generated, nouveau_util_py],
  output : ['lib.rs'],
  command : [
    prog_python, '@INPUT0@', '-p', nouveau_util_py_path,
    '--out-rs', '@OUTPUT0@', _nak_lat_sms.keys()
  ],
)

libnak_latencies_rs = static_library(
  'nak_latencies',
  _nak_latencies_lib_rs,
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : rust_global_args,
)
