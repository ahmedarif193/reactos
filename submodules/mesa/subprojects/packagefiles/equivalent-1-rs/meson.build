project(
  'equivalent',
  'rust',
  version : '1.0.1',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

lib = static_library(
  'equivalent',
  'src/lib.rs',
  override_options : ['rust_std=2015', 'build.rust_std=2015', 'warning_level=0'],
  rust_abi : 'rust',
  native : true,
)

dep_equivalent = declare_dependency(
  link_with : [lib],
)
