project(
  'indexmap',
  'rust',
  version : '2.2.6',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

dep_equivalent = subproject('equivalent-1-rs').get_variable('dep_equivalent')
dep_hashbrown = subproject('hashbrown-0.14-rs').get_variable('dep_hashbrown')

rust_args = [
  '--cfg', 'feature="std"'
]

lib = static_library(
  'indexmap',
  'src/lib.rs',
  rust_args : rust_args,
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  rust_abi : 'rust',
  native : true,
  dependencies : [dep_equivalent, dep_hashbrown]
)

dep_indexmap = declare_dependency(
  link_with : [lib],
)
