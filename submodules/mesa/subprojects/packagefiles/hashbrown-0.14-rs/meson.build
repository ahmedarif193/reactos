project(
  'hashbrown',
  'rust',
  version : '0.14.1',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

rust_args = [
  '--cfg', 'feature="raw"'
]

lib = static_library(
  'hashbrown',
  'src/lib.rs',
  rust_args : rust_args,
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  rust_abi : 'rust',
  native : true,
)

dep_hashbrown = declare_dependency(
  link_with : [lib],
)
