project(
  'pest_meta',
  'rust',
  version : '2.8.0',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

once_cell = subproject('once_cell-1-rs').get_variable('lib')
pest = subproject('pest-2-rs').get_variable('lib')

lib = static_library(
  'pest_meta',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  link_with : [once_cell, pest],
  rust_abi : 'rust',
  native : true,
)

deb_pest_meta = declare_dependency(
  link_with : [lib, once_cell, pest],
)
