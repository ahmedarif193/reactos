project(
  'pest',
  'rust',
  version : '2.8.0',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

ucd = subproject('ucd-trie-0.1-rs').get_variable('lib')

lib = static_library(
  'pest',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  link_with : [ucd],
  rust_abi : 'rust',
  native : true,
)

dep_pest = declare_dependency(
  link_with : [lib, ucd],
)
