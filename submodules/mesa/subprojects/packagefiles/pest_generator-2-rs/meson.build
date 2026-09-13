project(
  'pest_generator',
  'rust',
  version : '2.8.0',
  license : 'MIT OR Apache-2.0',
  meson_version : '>= 1.7.0',
)

pest = subproject('pest-2-rs').get_variable('lib')
pest_meta = subproject('pest_meta-2-rs').get_variable('lib')
quote = subproject('quote-1-rs').get_variable('lib')
pm2 = subproject('proc-macro2-1-rs').get_variable('lib')
syn = subproject('syn-2-rs').get_variable('lib')

rust_args = [
  '--cfg', 'feature="std"',
]

lib = static_library(
  'pest_generator',
  'src/lib.rs',
  rust_args : rust_args,
  override_options : ['rust_std=2021', 'build.rust_std=2021', 'warning_level=0'],
  link_with : [pest, pest_meta, pm2, quote, syn],
  rust_abi : 'rust',
  native : true,
)

dep_pest_generator = declare_dependency(
  link_with : [lib, pest, pest_meta, quote, syn],
)
